// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 1999-2011, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 *   Date        Name        Description
 *   11/17/99    aliu        Creation.
 **********************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/rep.h"
#include "unicode/unifilt.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"
#include "rbt_rule.h"
#include "rbt_data.h"
#include "cmemory.h"
#include "strmatch.h"
#include "strrepl.h"
#include "util.h"
#include "putilimp.h"

static const char16_t FORWARD_OP[] = {32,62,32,0}; // " > "

U_NAMESPACE_BEGIN

/**
 * Construct a new rule with the given input, output text, and other
 * attributes.  A cursor position may be specified for the output text.
 * @param input input string, including key and optional ante and
 * post context
 * @param anteContextPos offset into input to end of ante context, or -1 if
 * none.  Must be <= input.length() if not -1.
 * @param postContextPos offset into input to start of post context, or -1
 * if none.  Must be <= input.length() if not -1, and must be >=
 * anteContextPos.
 * @param output output string
 * @param cursorPosition offset into output at which cursor is located, or -1 if
 * none.  If less than zero, then the cursor is placed after the
 * <code>output</code>; that is, -1 is equivalent to
 * <code>output.length()</code>.  If greater than
 * <code>output.length()</code> then an exception is thrown.
 * @param segs array of UnicodeFunctors corresponding to input pattern
 * segments, or null if there are none.  The array itself is adopted,
 * but the pointers within it are not.
 * @param segsCount number of elements in segs[]
 * @param anchorStart true if the the rule is anchored on the left to
 * the context start
 * @param anchorEnd true if the rule is anchored on the right to the
 * context limit
 */
TransliterationRule::TransliterationRule(const UnicodeString& input,
                                         int32_t anteContextPos, int32_t postContextPos,
                                         const UnicodeString& outputStr,
                                         int32_t cursorPosition, int32_t cursorOffset,
                                         UnicodeFunctor** segs,
                                         int32_t segsCount,
                                         UBool anchorStart, UBool anchorEnd,
                                         const TransliterationRuleData* theData,
                                         UErrorCode& status) :
    UMemory(),
    segments(nullptr),
    data(theData) {

    if (U_FAILURE(status)) {
        return;
    }
    // Do range checks only when warranted to save time
    if (anteContextPos < 0) {
        anteContextLength = 0;
    } else {
        if (anteContextPos > input.length()) {
            // throw new IllegalArgumentException("Invalid ante context");
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        anteContextLength = anteContextPos;
    }
    if (postContextPos < 0) {
        keyLength = input.length() - anteContextLength;
    } else {
        if (postContextPos < anteContextLength ||
            postContextPos > input.length()) {
            // throw new IllegalArgumentException("Invalid post context");
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        keyLength = postContextPos - anteContextLength;
    }
    if (cursorPosition < 0) {
        cursorPosition = outputStr.length();
    } else if (cursorPosition > outputStr.length()) {
        // throw new IllegalArgumentException("Invalid cursor position");
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    // We don't validate the segments array.  The caller must
    // guarantee that the segments are well-formed (that is, that
    // all $n references in the output refer to indices of this
    // array, and that no array elements are null).
    this->segments = segs;
    this->segmentsCount = segsCount;

    pattern = input;
    flags = 0;
    if (anchorStart) {
        flags |= ANCHOR_START;
    }
    if (anchorEnd) {
        flags |= ANCHOR_END;
    }

    anteContext = nullptr;
    if (anteContextLength > 0) {
        anteContext = new StringMatcher(pattern, 0, anteContextLength,
                                        false, *data);
        /* test for nullptr */
        if (anteContext == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }
    
    key = nullptr;
    if (keyLength > 0) {
        key = new StringMatcher(pattern, anteContextLength, anteContextLength + keyLength,
                                false, *data);
        /* test for nullptr */
        if (key == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }
    
    int32_t postContextLength = pattern.length() - keyLength - anteContextLength;
    postContext = nullptr;
    if (postContextLength > 0) {
        postContext = new StringMatcher(pattern, anteContextLength + keyLength, pattern.length(),
                                        false, *data);
        /* test for nullptr */
        if (postContext == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    this->output = new StringReplacer(outputStr, cursorPosition + cursorOffset, data);
    /* test for nullptr */
    if (this->output == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
}

/**
 * Copy constructor.
 */
TransliterationRule::TransliterationRule(TransliterationRule& other) :
    UMemory(other),
    anteContext(nullptr),
    key(nullptr),
    postContext(nullptr),
    pattern(other.pattern),
    anteContextLength(other.anteContextLength),
    keyLength(other.keyLength),
    flags(other.flags),
    data(other.data) {

    segments = nullptr;
    segmentsCount = 0;
    if (other.segmentsCount > 0) {
        segments = (UnicodeFunctor **)uprv_malloc(other.segmentsCount * sizeof(UnicodeFunctor *));
        uprv_memcpy(segments, other.segments, (size_t)other.segmentsCount*sizeof(segments[0]));
    }

    if (other.anteContext != nullptr) {
        anteContext = other.anteContext->clone();
    }
    if (other.key != nullptr) {
        key = other.key->clone();
    }
    if (other.postContext != nullptr) {
        postContext = other.postContext->clone();
    }
    output = other.output->clone();
}

TransliterationRule::~TransliterationRule() {
    uprv_free(segments);
    delete anteContext;
    delete key;
    delete postContext;
    delete output;
}

/**
 * Return the preceding context length.  This method is needed to
 * support the <code>Transliterator</code> method
 * <code>getMaximumContextLength()</code>.  Internally, this is
 * implemented as the anteContextLength, optionally plus one if
 * there is a start anchor.  The one character anchor gap is
 * needed to make repeated incremental transliteration with
 * anchors work.
 */
int32_t TransliterationRule::getContextLength() const {
    return anteContextLength + ((flags & ANCHOR_START) ? 1 : 0);
}

/**
 * Internal method.  Returns 8-bit index value for this rule.
 * This is the low byte of the first character of the key,
 * unless the first character of the key is a set.  If it's a
 * set, or otherwise can match multiple keys, the index value is -1.
 */
int16_t TransliterationRule::getIndexValue() const {
    if (anteContextLength == pattern.length()) {
        // A pattern with just ante context {such as foo)>bar} can
        // match any key.
        return -1;
    }
    UChar32 c = pattern.char32At(anteContextLength);
    return (int16_t)(data->lookupMatcher(c) == nullptr ? (c & 0xFF) : -1);
}

/**
 * Internal method.  Returns true if this rule matches the given
 * index value.  The index value is an 8-bit integer, 0..255,
 * representing the low byte of the first character of the key.
 * It matches this rule if it matches the first character of the
 * key, or if the first character of the key is a set, and the set
 * contains any character with a low byte equal to the index
 * value.  If the rule contains only ante context, as in foo)>bar,
 * then it will match any key.
 */
UBool TransliterationRule::matchesIndexValue(uint8_t v) const {
    // Delegate to the key, or if there is none, to the postContext.
    // If there is neither then we match any key; return true.
    UnicodeMatcher *m = (key != nullptr) ? key : postContext;
    return (m != nullptr) ? m->matchesIndexValue(v) : true;
}

/**
 * Return true if this rule masks another rule.  If r1 masks r2 then
 * r1 matches any input string that r2 matches.  If r1 masks r2 and r2 masks
 * r1 then r1 == r2.  Examples: "a>x" masks "ab>y".  "a>x" masks "a[b]>y".
 * "[c]a>x" masks "[dc]a>y".
 */
UBool TransliterationRule::masks(const TransliterationRule& r2) const {
    /* Rule r1 masks rule r2 if the string formed of the
     * antecontext, key, and postcontext overlaps in the following
     * way:
     *
     * r1:      aakkkpppp
     * r2:     aaakkkkkpppp
     *            ^
     * 
     * The strings must be aligned at the first character of the
     * key.  The length of r1 to the left of the alignment point
     * must be <= the length of r2 to the left; ditto for the
     * right.  The characters of r1 must equal (or be a superset
     * of) the corresponding characters of r2.  The superset
     * operation should be performed to check for UnicodeSet
     * masking.
     *
     * Anchors:  Two patterns that differ only in anchors only
     * mask one another if they are exactly equal, and r2 has
     * all the anchors r1 has (optionally, plus some).  Here Y
     * means the row masks the column, N means it doesn't.
     *
     *         ab   ^ab    ab$  ^ab$
     *   ab    Y     Y     Y     Y
     *  ^ab    N     Y     N     Y
     *   ab$   N     N     Y     Y
     *  ^ab$   N     N     N     Y
     *
     * Post context: {a}b masks ab, but not vice versa, since {a}b
     * matches everything ab matches, and {a}b matches {|a|}b but ab
     * does not.  Pre context is different (a{b} does not align with
     * ab).
     */

    /* LIMITATION of the current mask algorithm: Some rule
     * maskings are currently not detected.  For example,
     * "{Lu}]a>x" masks "A]a>y".  This can be added later. TODO
     */

    int32_t len = pattern.length();
    int32_t left = anteContextLength;
    int32_t left2 = r2.anteContextLength;
    int32_t right = len - left;
    int32_t right2 = r2.pattern.length() - left2;
    int32_t cachedCompare = r2.pattern.compare(left2 - left, len, pattern);

    // TODO Clean this up -- some logic might be combinable with the
    // next statement.

    // Test for anchor masking
    if (left == left2 && right == right2 &&
        keyLength <= r2.keyLength &&
        0 == cachedCompare) {
        // The following boolean logic implements the table above
        return (flags == r2.flags) ||
            (!(flags & ANCHOR_START) && !(flags & ANCHOR_END)) ||
            ((r2.flags & ANCHOR_START) && (r2.flags & ANCHOR_END));
    }

    return left <= left2 &&
        (right < right2 ||
         (right == right2 && keyLength <= r2.keyLength)) &&
         (0 == cachedCompare);
}

static inline int32_t posBefore(const Replaceable& str, int32_t pos) {
    return (pos > 0) ?
        pos - U16_LENGTH(str.char32At(pos-1)) :
        pos - 1;
}

static inline int32_t posAfter(const Replaceable& str, int32_t pos) {
    return (pos >= 0 && pos < str.length()) ?
        pos + U16_LENGTH(str.char32At(pos)) :
        pos + 1;
}

/**
 * Attempt a match and replacement at the given position.  Return
 * the degree of match between this rule and the given text.  The
 * degree of match may be mismatch, a partial match, or a full
 * match.  A mismatch means at least one character of the text
 * does not match the context or key.  A partial match means some
 * context and key characters match, but the text is not long
 * enough to match all of them.  A full match means all context
 * and key characters match.
 * 
 * If a full match is obtained, perform a replacement, update pos,
 * and return U_MATCH.  Otherwise both text and pos are unchanged.
 * 
 * @param text the text
 * @param pos the position indices
 * @param incremental if true, test for partial matches that may
 * be completed by additional text inserted at pos.limit.
 * @return one of <code>U_MISMATCH</code>,
 * <code>U_PARTIAL_MATCH</code>, or <code>U_MATCH</code>.  If
 * incremental is false then U_PARTIAL_MATCH will not be returned.
 */
UMatchDegree TransliterationRule::matchAndReplace(Replaceable& text,
                                                  UTransPosition& pos,
                                                  UBool incremental) const {
    // Matching and replacing are done in one method because the
    // replacement operation needs information obtained during the
    // match.  Another way to do this is to have the match method
    // create a match result struct with relevant offsets, and to pass
    // this into the replace method.

    // ============================ MATCH ===========================

    // Reset segment match data
    if (segments != nullptr) {
        for (int32_t i=0; i<segmentsCount; ++i) {
            ((StringMatcher*) segments[i])->resetMatch();
        }
    }

//    int32_t lenDelta, keyLimit;
    int32_t keyLimit;

    // ------------------------ Ante Context ------------------------

    // A mismatch in the ante context, or with the start anchor,
    // is an outright U_MISMATCH regardless of whether we are
    // incremental or not.
    int32_t oText; // offset into 'text'
//    int32_t newStart = 0;
    int32_t minOText;

    // Note (1): We process text in 16-bit code units, rather than
    // 32-bit code points.  This works because stand-ins are
    // always in the BMP and because we are doing a literal match
    // operation, which can be done 16-bits at a time.
    
    int32_t anteLimit = posBefore(text, pos.contextStart);

    UMatchDegree match;

    // Start reverse match at char before pos.start
    oText = posBefore(text, pos.start);

    if (anteContext != nullptr) {
        match = anteContext->matches(text, oText, anteLimit, false);
        if (match != U_MATCH) {
            return U_MISMATCH;
        }
    }

    minOText = posAfter(text, oText);

    // ------------------------ Start Anchor ------------------------
    
    if (((flags & ANCHOR_START) != 0) && oText != anteLimit) {
        return U_MISMATCH;
    }

    // -------------------- Key and Post Context --------------------
    
    oText = pos.start;

    if (key != nullptr) {
        match = key->matches(text, oText, pos.limit, incremental);
        if (match != U_MATCH) {
            return match;
        }
    }

    keyLimit = oText;

    if (postContext != nullptr) {
        if (incremental && keyLimit == pos.limit) {
            // The key matches just before pos.limit, and there is
            // a postContext.  Since we are in incremental mode,
            // we must assume more characters may be inserted at
            // pos.limit -- this is a partial match.
            return U_PARTIAL_MATCH;
        }

        match = postContext->matches(text, oText, pos.contextLimit, incremental);
        if (match != U_MATCH) {
            return match;
        }
    }
    
    // ------------------------- Stop Anchor ------------------------
    
    if (((flags & ANCHOR_END)) != 0) {
        if (oText != pos.contextLimit) {
            return U_MISMATCH;
        }
        if (incremental) {
            return U_PARTIAL_MATCH;
        }
    }
    
    // =========================== REPLACE ==========================

    // We have a full match.  The key is between pos.start and
    // keyLimit.

    int32_t newStart;
    int32_t newLength = output->toReplacer()->replace(text, pos.start, keyLimit, newStart);
    int32_t lenDelta = newLength - (keyLimit - pos.start);

    oText += lenDelta;
    pos.limit += lenDelta;
    pos.contextLimit += lenDelta;
    // Restrict new value of start to [minOText, min(oText, pos.limit)].
    pos.start = uprv_max(minOText, uprv_min(uprv_min(oText, pos.limit), newStart));
    return U_MATCH;
}

/**
 * Create a source string that represents this rule.  Append it to the
 * given string.
 */
UnicodeString& TransliterationRule::toRule(UnicodeString& rule,
                                           UBool escapeUnprintable) const {

    // Accumulate special characters (and non-specials following them)
    // into quoteBuf.  Append quoteBuf, within single quotes, when
    // a non-quoted element must be inserted.
    UnicodeString str, quoteBuf;

    // Do not emit the braces '{' '}' around the pattern if there
    // is neither anteContext nor postContext.
    UBool emitBraces =
        (anteContext != nullptr) || (postContext != nullptr);

    // Emit start anchor
    if ((flags & ANCHOR_START) != 0) {
        rule.append((char16_t)94/*^*/);
    }

    // Emit the input pattern
    ICU_Utility::appendToRule(rule, anteContext, escapeUnprintable, quoteBuf);

    if (emitBraces) {
        ICU_Utility::appendToRule(rule, (char16_t) 0x007B /*{*/, true, escapeUnprintable, quoteBuf);
    }

    ICU_Utility::appendToRule(rule, key, escapeUnprintable, quoteBuf);

    if (emitBraces) {
        ICU_Utility::appendToRule(rule, (char16_t) 0x007D /*}*/, true, escapeUnprintable, quoteBuf);
    }

    ICU_Utility::appendToRule(rule, postContext, escapeUnprintable, quoteBuf);

    // Emit end anchor
    if ((flags & ANCHOR_END) != 0) {
        rule.append((char16_t)36/*$*/);
    }

    ICU_Utility::appendToRule(rule, UnicodeString(true, FORWARD_OP, 3), true, escapeUnprintable, quoteBuf);

    // Emit the output pattern

    ICU_Utility::appendToRule(rule, output->toReplacer()->toReplacerPattern(str, escapeUnprintable),
                              true, escapeUnprintable, quoteBuf);

    ICU_Utility::appendToRule(rule, (char16_t) 0x003B /*;*/, true, escapeUnprintable, quoteBuf);

    return rule;
}

void TransliterationRule::setData(const TransliterationRuleData* d) {
    data = d;
    if (anteContext != nullptr) anteContext->setData(d);
    if (postContext != nullptr) postContext->setData(d);
    if (key != nullptr) key->setData(d);
    // assert(output != nullptr);
    output->setData(d);
    // Don't have to do segments since they are in the context or key
}

/**
 * Union the set of all characters that may be modified by this rule
 * into the given set.
 */
void TransliterationRule::addSourceSetTo(UnicodeSet& toUnionTo) const {
    int32_t limit = anteContextLength + keyLength;
    for (int32_t i=anteContextLength; i<limit; ) {
        UChar32 ch = pattern.char32At(i);
        i += U16_LENGTH(ch);
        const UnicodeMatcher* matcher = data->lookupMatcher(ch);
        if (matcher == nullptr) {
            toUnionTo.add(ch);
        } else {
            matcher->addMatchSetTo(toUnionTo);
        }
    }
}

/**
 * Union the set of all characters that may be emitted by this rule
 * into the given set.
 */
void TransliterationRule::addTargetSetTo(UnicodeSet& toUnionTo) const {
    output->toReplacer()->addReplacementSetTo(toUnionTo);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
