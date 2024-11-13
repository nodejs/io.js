// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (c) 2001-2012, International Business Machines Corporation
*   and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   07/23/01    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "strmatch.h"
#include "rbt_data.h"
#include "util.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(StringMatcher)

StringMatcher::StringMatcher(const UnicodeString& theString,
                             int32_t start,
                             int32_t limit,
                             int32_t segmentNum,
                             const TransliterationRuleData& theData) :
    data(&theData),
    segmentNumber(segmentNum),
    matchStart(-1),
    matchLimit(-1)
{
    theString.extractBetween(start, limit, pattern);
}

StringMatcher::StringMatcher(const StringMatcher& o) :
    UnicodeFunctor(o),
    UnicodeMatcher(o),
    UnicodeReplacer(o),
    pattern(o.pattern),
    data(o.data),
    segmentNumber(o.segmentNumber),
    matchStart(o.matchStart),
    matchLimit(o.matchLimit)
{
}

/**
 * Destructor
 */
StringMatcher::~StringMatcher() {
}

/**
 * Implement UnicodeFunctor
 */
StringMatcher* StringMatcher::clone() const {
    return new StringMatcher(*this);
}

/**
 * UnicodeFunctor API.  Cast 'this' to a UnicodeMatcher* pointer
 * and return the pointer.
 */
UnicodeMatcher* StringMatcher::toMatcher() const {
  StringMatcher  *nonconst_this = const_cast<StringMatcher *>(this);
  UnicodeMatcher *nonconst_base = static_cast<UnicodeMatcher *>(nonconst_this);
  
  return nonconst_base;
}

/**
 * UnicodeFunctor API.  Cast 'this' to a UnicodeReplacer* pointer
 * and return the pointer.
 */
UnicodeReplacer* StringMatcher::toReplacer() const {
  StringMatcher  *nonconst_this = const_cast<StringMatcher *>(this);
  UnicodeReplacer *nonconst_base = static_cast<UnicodeReplacer *>(nonconst_this);
  
  return nonconst_base;
}

/**
 * Implement UnicodeMatcher
 */
UMatchDegree StringMatcher::matches(const Replaceable& text,
                                    int32_t& offset,
                                    int32_t limit,
                                    UBool incremental) {
    int32_t i;
    int32_t cursor = offset;
    if (limit < cursor) {
        // Match in the reverse direction
        for (i=pattern.length()-1; i>=0; --i) {
            char16_t keyChar = pattern.charAt(i);
            UnicodeMatcher* subm = data->lookupMatcher(keyChar);
            if (subm == nullptr) {
                if (cursor > limit &&
                    keyChar == text.charAt(cursor)) {
                    --cursor;
                } else {
                    return U_MISMATCH;
                }
            } else {
                UMatchDegree m =
                    subm->matches(text, cursor, limit, incremental);
                if (m != U_MATCH) {
                    return m;
                }
            }
        }
        // Record the match position, but adjust for a normal
        // forward start, limit, and only if a prior match does not
        // exist -- we want the rightmost match.
        if (matchStart < 0) {
            matchStart = cursor+1;
            matchLimit = offset+1;
        }
    } else {
        for (i=0; i<pattern.length(); ++i) {
            if (incremental && cursor == limit) {
                // We've reached the context limit without a mismatch and
                // without completing our match.
                return U_PARTIAL_MATCH;
            }
            char16_t keyChar = pattern.charAt(i);
            UnicodeMatcher* subm = data->lookupMatcher(keyChar);
            if (subm == nullptr) {
                // Don't need the cursor < limit check if
                // incremental is true (because it's done above); do need
                // it otherwise.
                if (cursor < limit &&
                    keyChar == text.charAt(cursor)) {
                    ++cursor;
                } else {
                    return U_MISMATCH;
                }
            } else {
                UMatchDegree m =
                    subm->matches(text, cursor, limit, incremental);
                if (m != U_MATCH) {
                    return m;
                }
            }
        }
        // Record the match position
        matchStart = offset;
        matchLimit = cursor;
    }

    offset = cursor;
    return U_MATCH;
}

/**
 * Implement UnicodeMatcher
 */
UnicodeString& StringMatcher::toPattern(UnicodeString& result,
                                        UBool escapeUnprintable) const
{
    result.truncate(0);
    UnicodeString str, quoteBuf;
    if (segmentNumber > 0) {
        result.append(static_cast<char16_t>(40)); /*(*/
    }
    for (int32_t i=0; i<pattern.length(); ++i) {
        char16_t keyChar = pattern.charAt(i);
        const UnicodeMatcher* m = data->lookupMatcher(keyChar);
        if (m == nullptr) {
            ICU_Utility::appendToRule(result, keyChar, false, escapeUnprintable, quoteBuf);
        } else {
            ICU_Utility::appendToRule(result, m->toPattern(str, escapeUnprintable),
                         true, escapeUnprintable, quoteBuf);
        }
    }
    if (segmentNumber > 0) {
        result.append(static_cast<char16_t>(41)); /*)*/
    }
    // Flush quoteBuf out to result
    ICU_Utility::appendToRule(result, -1,
                              true, escapeUnprintable, quoteBuf);
    return result;
}

/**
 * Implement UnicodeMatcher
 */
UBool StringMatcher::matchesIndexValue(uint8_t v) const {
    if (pattern.length() == 0) {
        return true;
    }
    UChar32 c = pattern.char32At(0);
    const UnicodeMatcher *m = data->lookupMatcher(c);
    return (m == nullptr) ? ((c & 0xFF) == v) : m->matchesIndexValue(v);
}

/**
 * Implement UnicodeMatcher
 */
void StringMatcher::addMatchSetTo(UnicodeSet& toUnionTo) const {
    UChar32 ch;
    for (int32_t i=0; i<pattern.length(); i+=U16_LENGTH(ch)) {
        ch = pattern.char32At(i);
        const UnicodeMatcher* matcher = data->lookupMatcher(ch);
        if (matcher == nullptr) {
            toUnionTo.add(ch);
        } else {
            matcher->addMatchSetTo(toUnionTo);
        }
    }
}

/**
 * UnicodeReplacer API
 */
int32_t StringMatcher::replace(Replaceable& text,
                               int32_t start,
                               int32_t limit,
                               int32_t& /*cursor*/) {
    
    int32_t outLen = 0;
    
    // Copy segment with out-of-band data
    int32_t dest = limit;
    // If there was no match, that means that a quantifier
    // matched zero-length.  E.g., x (a)* y matched "xy".
    if (matchStart >= 0) {
        if (matchStart != matchLimit) {
            text.copy(matchStart, matchLimit, dest);
            outLen = matchLimit - matchStart;
        }
    }
    
    text.handleReplaceBetween(start, limit, UnicodeString()); // delete original text
    
    return outLen;
}

/**
 * UnicodeReplacer API
 */
UnicodeString& StringMatcher::toReplacerPattern(UnicodeString& rule,
                                                UBool /*escapeUnprintable*/) const {
    // assert(segmentNumber > 0);
    rule.truncate(0);
    rule.append(static_cast<char16_t>(0x0024) /*$*/);
    ICU_Utility::appendNumber(rule, segmentNumber, 10, 1);
    return rule;
}

/**
 * Remove any match info.  This must be called before performing a
 * set of matches with this segment.
 */
 void StringMatcher::resetMatch() {
    matchStart = matchLimit = -1;
}

/**
 * Union the set of all characters that may output by this object
 * into the given set.
 * @param toUnionTo the set into which to union the output characters
 */
void StringMatcher::addReplacementSetTo(UnicodeSet& /*toUnionTo*/) const {
    // The output of this replacer varies; it is the source text between
    // matchStart and matchLimit.  Since this varies depending on the
    // input text, we can't compute it here.  We can either do nothing
    // or we can add ALL characters to the set.  It's probably more useful
    // to do nothing.
}

/**
 * Implement UnicodeFunctor
 */
void StringMatcher::setData(const TransliterationRuleData* d) {
    data = d;
    int32_t i = 0;
    while (i<pattern.length()) {
        UChar32 c = pattern.char32At(i);
        UnicodeFunctor* f = data->lookup(c);
        if (f != nullptr) {
            f->setData(data);
        }
        i += U16_LENGTH(c);
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
