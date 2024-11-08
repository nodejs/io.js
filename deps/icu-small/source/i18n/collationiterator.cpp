// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationiterator.cpp
*
* created on: 2010oct27
* created by: Markus W. Scherer
*/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucharstrie.h"
#include "unicode/ustringtrie.h"
#include "charstr.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "collationfcd.h"
#include "collationiterator.h"
#include "normalizer2impl.h"
#include "uassert.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN

CollationIterator::CEBuffer::~CEBuffer() {}

UBool
CollationIterator::CEBuffer::ensureAppendCapacity(int32_t appCap, UErrorCode &errorCode) {
    int32_t capacity = buffer.getCapacity();
    if((length + appCap) <= capacity) { return true; }
    if(U_FAILURE(errorCode)) { return false; }
    do {
        if(capacity < 1000) {
            capacity *= 4;
        } else {
            capacity *= 2;
        }
    } while(capacity < (length + appCap));
    int64_t *p = buffer.resize(capacity, length);
    if(p == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    return true;
}

// State of combining marks skipped in discontiguous contraction.
// We create a state object on first use and keep it around deactivated between uses.
class SkippedState : public UMemory {
public:
    // Born active but empty.
    SkippedState() : pos(0), skipLengthAtMatch(0) {}
    void clear() {
        oldBuffer.remove();
        pos = 0;
        // The newBuffer is reset by setFirstSkipped().
    }

    UBool isEmpty() const { return oldBuffer.isEmpty(); }

    UBool hasNext() const { return pos < oldBuffer.length(); }

    // Requires hasNext().
    UChar32 next() {
        UChar32 c = oldBuffer.char32At(pos);
        pos += U16_LENGTH(c);
        return c;
    }

    // Accounts for one more input code point read beyond the end of the marks buffer.
    void incBeyond() {
        U_ASSERT(!hasNext());
        ++pos;
    }

    // Goes backward through the skipped-marks buffer.
    // Returns the number of code points read beyond the skipped marks
    // that need to be backtracked through normal input.
    int32_t backwardNumCodePoints(int32_t n) {
        int32_t length = oldBuffer.length();
        int32_t beyond = pos - length;
        if(beyond > 0) {
            if(beyond >= n) {
                // Not back far enough to re-enter the oldBuffer.
                pos -= n;
                return n;
            } else {
                // Back out all beyond-oldBuffer code points and re-enter the buffer.
                pos = oldBuffer.moveIndex32(length, beyond - n);
                return beyond;
            }
        } else {
            // Go backwards from inside the oldBuffer.
            pos = oldBuffer.moveIndex32(pos, -n);
            return 0;
        }
    }

    void setFirstSkipped(UChar32 c) {
        skipLengthAtMatch = 0;
        newBuffer.setTo(c);
    }

    void skip(UChar32 c) {
        newBuffer.append(c);
    }

    void recordMatch() { skipLengthAtMatch = newBuffer.length(); }

    // Replaces the characters we consumed with the newly skipped ones.
    void replaceMatch() {
        // Note: UnicodeString.replace() pins pos to at most length().
        oldBuffer.replace(0, pos, newBuffer, 0, skipLengthAtMatch);
        pos = 0;
    }

    void saveTrieState(const UCharsTrie &trie) { trie.saveState(state); }
    void resetToTrieState(UCharsTrie &trie) const { trie.resetToState(state); }

private:
    // Combining marks skipped in previous discontiguous-contraction matching.
    // After that discontiguous contraction was completed, we start reading them from here.
    UnicodeString oldBuffer;
    // Combining marks newly skipped in current discontiguous-contraction matching.
    // These might have been read from the normal text or from the oldBuffer.
    UnicodeString newBuffer;
    // Reading index in oldBuffer,
    // or counter for how many code points have been read beyond oldBuffer (pos-oldBuffer.length()).
    int32_t pos;
    // newBuffer.length() at the time of the last matching character.
    // When a partial match fails, we back out skipped and partial-matching input characters.
    int32_t skipLengthAtMatch;
    // We save the trie state before we attempt to match a character,
    // so that we can skip it and try the next one.
    UCharsTrie::State state;
};

CollationIterator::CollationIterator(const CollationIterator &other)
        : UObject(other),
          trie(other.trie),
          data(other.data),
          cesIndex(other.cesIndex),
          skipped(nullptr),
          numCpFwd(other.numCpFwd),
          isNumeric(other.isNumeric) {
    UErrorCode errorCode = U_ZERO_ERROR;
    int32_t length = other.ceBuffer.length;
    if(length > 0 && ceBuffer.ensureAppendCapacity(length, errorCode)) {
        for(int32_t i = 0; i < length; ++i) {
            ceBuffer.set(i, other.ceBuffer.get(i));
        }
        ceBuffer.length = length;
    } else {
        cesIndex = 0;
    }
}

CollationIterator::~CollationIterator() {
    delete skipped;
}

bool
CollationIterator::operator==(const CollationIterator &other) const {
    // Subclasses: Call this method and then add more specific checks.
    // Compare the iterator state but not the collation data (trie & data fields):
    // Assume that the caller compares the data.
    // Ignore skipped since that should be unused between calls to nextCE().
    // (It only stays around to avoid another memory allocation.)
    if(!(typeid(*this) == typeid(other) &&
            ceBuffer.length == other.ceBuffer.length &&
            cesIndex == other.cesIndex &&
            numCpFwd == other.numCpFwd &&
            isNumeric == other.isNumeric)) {
        return false;
    }
    for(int32_t i = 0; i < ceBuffer.length; ++i) {
        if(ceBuffer.get(i) != other.ceBuffer.get(i)) { return false; }
    }
    return true;
}

void
CollationIterator::reset() {
    cesIndex = ceBuffer.length = 0;
    if(skipped != nullptr) { skipped->clear(); }
}

int32_t
CollationIterator::fetchCEs(UErrorCode &errorCode) {
    while(U_SUCCESS(errorCode) && nextCE(errorCode) != Collation::NO_CE) {
        // No need to loop for each expansion CE.
        cesIndex = ceBuffer.length;
    }
    return ceBuffer.length;
}

uint32_t
CollationIterator::handleNextCE32(UChar32 &c, UErrorCode &errorCode) {
    c = nextCodePoint(errorCode);
    return (c < 0) ? Collation::FALLBACK_CE32 : data->getCE32(c);
}

char16_t
CollationIterator::handleGetTrailSurrogate() {
    return 0;
}

UBool
CollationIterator::foundNULTerminator() {
    return false;
}

UBool
CollationIterator::forbidSurrogateCodePoints() const {
    return false;
}

uint32_t
CollationIterator::getDataCE32(UChar32 c) const {
    return data->getCE32(c);
}

uint32_t
CollationIterator::getCE32FromBuilderData(uint32_t /*ce32*/, UErrorCode &errorCode) {
    if(U_SUCCESS(errorCode)) { errorCode = U_INTERNAL_PROGRAM_ERROR; }
    return 0;
}

int64_t
CollationIterator::nextCEFromCE32(const CollationData *d, UChar32 c, uint32_t ce32,
                                  UErrorCode &errorCode) {
    --ceBuffer.length;  // Undo ceBuffer.incLength().
    appendCEsFromCE32(d, c, ce32, true, errorCode);
    if(U_SUCCESS(errorCode)) {
        return ceBuffer.get(cesIndex++);
    } else {
        return Collation::NO_CE_PRIMARY;
    }
}

void
CollationIterator::appendCEsFromCE32(const CollationData *d, UChar32 c, uint32_t ce32,
                                     UBool forward, UErrorCode &errorCode) {
    while(Collation::isSpecialCE32(ce32)) {
        switch(Collation::tagFromCE32(ce32)) {
        case Collation::FALLBACK_TAG:
        case Collation::RESERVED_TAG_3:
            if(U_SUCCESS(errorCode)) { errorCode = U_INTERNAL_PROGRAM_ERROR; }
            return;
        case Collation::LONG_PRIMARY_TAG:
            ceBuffer.append(Collation::ceFromLongPrimaryCE32(ce32), errorCode);
            return;
        case Collation::LONG_SECONDARY_TAG:
            ceBuffer.append(Collation::ceFromLongSecondaryCE32(ce32), errorCode);
            return;
        case Collation::LATIN_EXPANSION_TAG:
            if(ceBuffer.ensureAppendCapacity(2, errorCode)) {
                ceBuffer.set(ceBuffer.length, Collation::latinCE0FromCE32(ce32));
                ceBuffer.set(ceBuffer.length + 1, Collation::latinCE1FromCE32(ce32));
                ceBuffer.length += 2;
            }
            return;
        case Collation::EXPANSION32_TAG: {
            const uint32_t *ce32s = d->ce32s + Collation::indexFromCE32(ce32);
            int32_t length = Collation::lengthFromCE32(ce32);
            if(ceBuffer.ensureAppendCapacity(length, errorCode)) {
                do {
                    ceBuffer.appendUnsafe(Collation::ceFromCE32(*ce32s++));
                } while(--length > 0);
            }
            return;
        }
        case Collation::EXPANSION_TAG: {
            const int64_t *ces = d->ces + Collation::indexFromCE32(ce32);
            int32_t length = Collation::lengthFromCE32(ce32);
            if(ceBuffer.ensureAppendCapacity(length, errorCode)) {
                do {
                    ceBuffer.appendUnsafe(*ces++);
                } while(--length > 0);
            }
            return;
        }
        case Collation::BUILDER_DATA_TAG:
            ce32 = getCE32FromBuilderData(ce32, errorCode);
            if(U_FAILURE(errorCode)) { return; }
            if(ce32 == Collation::FALLBACK_CE32) {
                d = data->base;
                ce32 = d->getCE32(c);
            }
            break;
        case Collation::PREFIX_TAG:
            if(forward) { backwardNumCodePoints(1, errorCode); }
            ce32 = getCE32FromPrefix(d, ce32, errorCode);
            if(forward) { forwardNumCodePoints(1, errorCode); }
            break;
        case Collation::CONTRACTION_TAG: {
            const char16_t *p = d->contexts + Collation::indexFromCE32(ce32);
            uint32_t defaultCE32 = CollationData::readCE32(p);  // Default if no suffix match.
            if(!forward) {
                // Backward contractions are handled by previousCEUnsafe().
                // c has contractions but they were not found.
                ce32 = defaultCE32;
                break;
            }
            UChar32 nextCp;
            if(skipped == nullptr && numCpFwd < 0) {
                // Some portion of nextCE32FromContraction() pulled out here as an ASCII fast path,
                // avoiding the function call and the nextSkippedCodePoint() overhead.
                nextCp = nextCodePoint(errorCode);
                if(nextCp < 0) {
                    // No more text.
                    ce32 = defaultCE32;
                    break;
                } else if((ce32 & Collation::CONTRACT_NEXT_CCC) != 0 &&
                        !CollationFCD::mayHaveLccc(nextCp)) {
                    // All contraction suffixes start with characters with lccc!=0
                    // but the next code point has lccc==0.
                    backwardNumCodePoints(1, errorCode);
                    ce32 = defaultCE32;
                    break;
                }
            } else {
                nextCp = nextSkippedCodePoint(errorCode);
                if(nextCp < 0) {
                    // No more text.
                    ce32 = defaultCE32;
                    break;
                } else if((ce32 & Collation::CONTRACT_NEXT_CCC) != 0 &&
                        !CollationFCD::mayHaveLccc(nextCp)) {
                    // All contraction suffixes start with characters with lccc!=0
                    // but the next code point has lccc==0.
                    backwardNumSkipped(1, errorCode);
                    ce32 = defaultCE32;
                    break;
                }
            }
            ce32 = nextCE32FromContraction(d, ce32, p + 2, defaultCE32, nextCp, errorCode);
            if(ce32 == Collation::NO_CE32) {
                // CEs from a discontiguous contraction plus the skipped combining marks
                // have been appended already.
                return;
            }
            break;
        }
        case Collation::DIGIT_TAG:
            if(isNumeric) {
                appendNumericCEs(ce32, forward, errorCode);
                return;
            } else {
                // Fetch the non-numeric-collation CE32 and continue.
                ce32 = d->ce32s[Collation::indexFromCE32(ce32)];
                break;
            }
        case Collation::U0000_TAG:
            U_ASSERT(c == 0);
            if(forward && foundNULTerminator()) {
                // Handle NUL-termination. (Not needed in Java.)
                ceBuffer.append(Collation::NO_CE, errorCode);
                return;
            } else {
                // Fetch the normal ce32 for U+0000 and continue.
                ce32 = d->ce32s[0];
                break;
            }
        case Collation::HANGUL_TAG: {
            const uint32_t *jamoCE32s = d->jamoCE32s;
            c -= Hangul::HANGUL_BASE;
            UChar32 t = c % Hangul::JAMO_T_COUNT;
            c /= Hangul::JAMO_T_COUNT;
            UChar32 v = c % Hangul::JAMO_V_COUNT;
            c /= Hangul::JAMO_V_COUNT;
            if((ce32 & Collation::HANGUL_NO_SPECIAL_JAMO) != 0) {
                // None of the Jamo CE32s are isSpecialCE32().
                // Avoid recursive function calls and per-Jamo tests.
                if(ceBuffer.ensureAppendCapacity(t == 0 ? 2 : 3, errorCode)) {
                    ceBuffer.set(ceBuffer.length, Collation::ceFromCE32(jamoCE32s[c]));
                    ceBuffer.set(ceBuffer.length + 1, Collation::ceFromCE32(jamoCE32s[19 + v]));
                    ceBuffer.length += 2;
                    if(t != 0) {
                        ceBuffer.appendUnsafe(Collation::ceFromCE32(jamoCE32s[39 + t]));
                    }
                }
                return;
            } else {
                // We should not need to compute each Jamo code point.
                // In particular, there should be no offset or implicit ce32.
                appendCEsFromCE32(d, U_SENTINEL, jamoCE32s[c], forward, errorCode);
                appendCEsFromCE32(d, U_SENTINEL, jamoCE32s[19 + v], forward, errorCode);
                if(t == 0) { return; }
                // offset 39 = 19 + 21 - 1:
                // 19 = JAMO_L_COUNT
                // 21 = JAMO_T_COUNT
                // -1 = omit t==0
                ce32 = jamoCE32s[39 + t];
                c = U_SENTINEL;
                break;
            }
        }
        case Collation::LEAD_SURROGATE_TAG: {
            U_ASSERT(forward);  // Backward iteration should never see lead surrogate code _unit_ data.
            U_ASSERT(U16_IS_LEAD(c));
            char16_t trail;
            if(U16_IS_TRAIL(trail = handleGetTrailSurrogate())) {
                c = U16_GET_SUPPLEMENTARY(c, trail);
                ce32 &= Collation::LEAD_TYPE_MASK;
                if(ce32 == Collation::LEAD_ALL_UNASSIGNED) {
                    ce32 = Collation::UNASSIGNED_CE32;  // unassigned-implicit
                } else if(ce32 == Collation::LEAD_ALL_FALLBACK ||
                        (ce32 = d->getCE32FromSupplementary(c)) == Collation::FALLBACK_CE32) {
                    // fall back to the base data
                    d = d->base;
                    ce32 = d->getCE32FromSupplementary(c);
                }
            } else {
                // c is an unpaired surrogate.
                ce32 = Collation::UNASSIGNED_CE32;
            }
            break;
        }
        case Collation::OFFSET_TAG:
            U_ASSERT(c >= 0);
            ceBuffer.append(d->getCEFromOffsetCE32(c, ce32), errorCode);
            return;
        case Collation::IMPLICIT_TAG:
            U_ASSERT(c >= 0);
            if(U_IS_SURROGATE(c) && forbidSurrogateCodePoints()) {
                ce32 = Collation::FFFD_CE32;
                break;
            } else {
                ceBuffer.append(Collation::unassignedCEFromCodePoint(c), errorCode);
                return;
            }
        }
    }
    ceBuffer.append(Collation::ceFromSimpleCE32(ce32), errorCode);
}

uint32_t
CollationIterator::getCE32FromPrefix(const CollationData *d, uint32_t ce32,
                                     UErrorCode &errorCode) {
    const char16_t *p = d->contexts + Collation::indexFromCE32(ce32);
    ce32 = CollationData::readCE32(p);  // Default if no prefix match.
    p += 2;
    // Number of code points read before the original code point.
    int32_t lookBehind = 0;
    UCharsTrie prefixes(p);
    for(;;) {
        UChar32 c = previousCodePoint(errorCode);
        if(c < 0) { break; }
        ++lookBehind;
        UStringTrieResult match = prefixes.nextForCodePoint(c);
        if(USTRINGTRIE_HAS_VALUE(match)) {
            ce32 = static_cast<uint32_t>(prefixes.getValue());
        }
        if(!USTRINGTRIE_HAS_NEXT(match)) { break; }
    }
    forwardNumCodePoints(lookBehind, errorCode);
    return ce32;
}

UChar32
CollationIterator::nextSkippedCodePoint(UErrorCode &errorCode) {
    if(skipped != nullptr && skipped->hasNext()) { return skipped->next(); }
    if(numCpFwd == 0) { return U_SENTINEL; }
    UChar32 c = nextCodePoint(errorCode);
    if(skipped != nullptr && !skipped->isEmpty() && c >= 0) { skipped->incBeyond(); }
    if(numCpFwd > 0 && c >= 0) { --numCpFwd; }
    return c;
}

void
CollationIterator::backwardNumSkipped(int32_t n, UErrorCode &errorCode) {
    if(skipped != nullptr && !skipped->isEmpty()) {
        n = skipped->backwardNumCodePoints(n);
    }
    backwardNumCodePoints(n, errorCode);
    if(numCpFwd >= 0) { numCpFwd += n; }
}

uint32_t
CollationIterator::nextCE32FromContraction(const CollationData *d, uint32_t contractionCE32,
                                           const char16_t *p, uint32_t ce32, UChar32 c,
                                           UErrorCode &errorCode) {
    // c: next code point after the original one

    // Number of code points read beyond the original code point.
    // Needed for discontiguous contraction matching.
    int32_t lookAhead = 1;
    // Number of code points read since the last match (initially only c).
    int32_t sinceMatch = 1;
    // Normally we only need a contiguous match,
    // and therefore need not remember the suffixes state from before a mismatch for retrying.
    // If we are already processing skipped combining marks, then we do track the state.
    UCharsTrie suffixes(p);
    if(skipped != nullptr && !skipped->isEmpty()) { skipped->saveTrieState(suffixes); }
    UStringTrieResult match = suffixes.firstForCodePoint(c);
    for(;;) {
        UChar32 nextCp;
        if(USTRINGTRIE_HAS_VALUE(match)) {
            ce32 = static_cast<uint32_t>(suffixes.getValue());
            if(!USTRINGTRIE_HAS_NEXT(match) || (c = nextSkippedCodePoint(errorCode)) < 0) {
                return ce32;
            }
            if(skipped != nullptr && !skipped->isEmpty()) { skipped->saveTrieState(suffixes); }
            sinceMatch = 1;
        } else if(match == USTRINGTRIE_NO_MATCH || (nextCp = nextSkippedCodePoint(errorCode)) < 0) {
            // No match for c, or partial match (USTRINGTRIE_NO_VALUE) and no further text.
            // Back up if necessary, and try a discontiguous contraction.
            if((contractionCE32 & Collation::CONTRACT_TRAILING_CCC) != 0 &&
                    // Discontiguous contraction matching extends an existing match.
                    // If there is no match yet, then there is nothing to do.
                    ((contractionCE32 & Collation::CONTRACT_SINGLE_CP_NO_MATCH) == 0 ||
                        sinceMatch < lookAhead)) {
                // The last character of at least one suffix has lccc!=0,
                // allowing for discontiguous contractions.
                // UCA S2.1.1 only processes non-starters immediately following
                // "a match in the table" (sinceMatch=1).
                if(sinceMatch > 1) {
                    // Return to the state after the last match.
                    // (Return to sinceMatch=0 and re-fetch the first partially-matched character.)
                    backwardNumSkipped(sinceMatch, errorCode);
                    c = nextSkippedCodePoint(errorCode);
                    lookAhead -= sinceMatch - 1;
                    sinceMatch = 1;
                }
                if(d->getFCD16(c) > 0xff) {
                    return nextCE32FromDiscontiguousContraction(
                        d, suffixes, ce32, lookAhead, c, errorCode);
                }
            }
            break;
        } else {
            // Continue after partial match (USTRINGTRIE_NO_VALUE) for c.
            // It does not have a result value, therefore it is not itself "a match in the table".
            // If a partially-matched c has ccc!=0 then
            // it might be skipped in discontiguous contraction.
            c = nextCp;
            ++sinceMatch;
        }
        ++lookAhead;
        match = suffixes.nextForCodePoint(c);
    }
    backwardNumSkipped(sinceMatch, errorCode);
    return ce32;
}

uint32_t
CollationIterator::nextCE32FromDiscontiguousContraction(
        const CollationData *d, UCharsTrie &suffixes, uint32_t ce32,
        int32_t lookAhead, UChar32 c,
        UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }

    // UCA section 3.3.2 Contractions:
    // Contractions that end with non-starter characters
    // are known as discontiguous contractions.
    // ... discontiguous contractions must be detected in input text
    // whenever the final sequence of non-starter characters could be rearranged
    // so as to make a contiguous matching sequence that is canonically equivalent.

    // UCA: http://www.unicode.org/reports/tr10/#S2.1
    // S2.1 Find the longest initial substring S at each point that has a match in the table.
    // S2.1.1 If there are any non-starters following S, process each non-starter C.
    // S2.1.2 If C is not blocked from S, find if S + C has a match in the table.
    //     Note: A non-starter in a string is called blocked
    //     if there is another non-starter of the same canonical combining class or zero
    //     between it and the last character of canonical combining class 0.
    // S2.1.3 If there is a match, replace S by S + C, and remove C.

    // First: Is a discontiguous contraction even possible?
    uint16_t fcd16 = d->getFCD16(c);
    U_ASSERT(fcd16 > 0xff);  // The caller checked this already, as a shortcut.
    UChar32 nextCp = nextSkippedCodePoint(errorCode);
    if(nextCp < 0) {
        // No further text.
        backwardNumSkipped(1, errorCode);
        return ce32;
    }
    ++lookAhead;
    uint8_t prevCC = static_cast<uint8_t>(fcd16);
    fcd16 = d->getFCD16(nextCp);
    if(fcd16 <= 0xff) {
        // The next code point after c is a starter (S2.1.1 "process each non-starter").
        backwardNumSkipped(2, errorCode);
        return ce32;
    }

    // We have read and matched (lookAhead-2) code points,
    // read non-matching c and peeked ahead at nextCp.
    // Return to the state before the mismatch and continue matching with nextCp.
    if(skipped == nullptr || skipped->isEmpty()) {
        if(skipped == nullptr) {
            skipped = new SkippedState();
            if(skipped == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return 0;
            }
        }
        suffixes.reset();
        if(lookAhead > 2) {
            // Replay the partial match so far.
            backwardNumCodePoints(lookAhead, errorCode);
            suffixes.firstForCodePoint(nextCodePoint(errorCode));
            for(int32_t i = 3; i < lookAhead; ++i) {
                suffixes.nextForCodePoint(nextCodePoint(errorCode));
            }
            // Skip c (which did not match) and nextCp (which we will try now).
            forwardNumCodePoints(2, errorCode);
        }
        skipped->saveTrieState(suffixes);
    } else {
        // Reset to the trie state before the failed match of c.
        skipped->resetToTrieState(suffixes);
    }

    skipped->setFirstSkipped(c);
    // Number of code points read since the last match (at this point: c and nextCp).
    int32_t sinceMatch = 2;
    c = nextCp;
    for(;;) {
        UStringTrieResult match;
        // "If C is not blocked from S, find if S + C has a match in the table." (S2.1.2)
        if(prevCC < (fcd16 >> 8) && USTRINGTRIE_HAS_VALUE(match = suffixes.nextForCodePoint(c))) {
            // "If there is a match, replace S by S + C, and remove C." (S2.1.3)
            // Keep prevCC unchanged.
            ce32 = static_cast<uint32_t>(suffixes.getValue());
            sinceMatch = 0;
            skipped->recordMatch();
            if(!USTRINGTRIE_HAS_NEXT(match)) { break; }
            skipped->saveTrieState(suffixes);
        } else {
            // No match for "S + C", skip C.
            skipped->skip(c);
            skipped->resetToTrieState(suffixes);
            prevCC = static_cast<uint8_t>(fcd16);
        }
        if((c = nextSkippedCodePoint(errorCode)) < 0) { break; }
        ++sinceMatch;
        fcd16 = d->getFCD16(c);
        if(fcd16 <= 0xff) {
            // The next code point after c is a starter (S2.1.1 "process each non-starter").
            break;
        }
    }
    backwardNumSkipped(sinceMatch, errorCode);
    UBool isTopDiscontiguous = skipped->isEmpty();
    skipped->replaceMatch();
    if(isTopDiscontiguous && !skipped->isEmpty()) {
        // We did get a match after skipping one or more combining marks,
        // and we are not in a recursive discontiguous contraction.
        // Append CEs from the contraction ce32
        // and then from the combining marks that we skipped before the match.
        c = U_SENTINEL;
        for(;;) {
            appendCEsFromCE32(d, c, ce32, true, errorCode);
            // Fetch CE32s for skipped combining marks from the normal data, with fallback,
            // rather than from the CollationData where we found the contraction.
            if(!skipped->hasNext()) { break; }
            c = skipped->next();
            ce32 = getDataCE32(c);
            if(ce32 == Collation::FALLBACK_CE32) {
                d = data->base;
                ce32 = d->getCE32(c);
            } else {
                d = data;
            }
            // Note: A nested discontiguous-contraction match
            // replaces consumed combining marks with newly skipped ones
            // and resets the reading position to the beginning.
        }
        skipped->clear();
        ce32 = Collation::NO_CE32;  // Signal to the caller that the result is in the ceBuffer.
    }
    return ce32;
}

void
CollationIterator::appendNumericCEs(uint32_t ce32, UBool forward, UErrorCode &errorCode) {
    // Collect digits.
    CharString digits;
    if(forward) {
        for(;;) {
            char digit = Collation::digitFromCE32(ce32);
            digits.append(digit, errorCode);
            if(numCpFwd == 0) { break; }
            UChar32 c = nextCodePoint(errorCode);
            if(c < 0) { break; }
            ce32 = data->getCE32(c);
            if(ce32 == Collation::FALLBACK_CE32) {
                ce32 = data->base->getCE32(c);
            }
            if(!Collation::hasCE32Tag(ce32, Collation::DIGIT_TAG)) {
                backwardNumCodePoints(1, errorCode);
                break;
            }
            if(numCpFwd > 0) { --numCpFwd; }
        }
    } else {
        for(;;) {
            char digit = Collation::digitFromCE32(ce32);
            digits.append(digit, errorCode);
            UChar32 c = previousCodePoint(errorCode);
            if(c < 0) { break; }
            ce32 = data->getCE32(c);
            if(ce32 == Collation::FALLBACK_CE32) {
                ce32 = data->base->getCE32(c);
            }
            if(!Collation::hasCE32Tag(ce32, Collation::DIGIT_TAG)) {
                forwardNumCodePoints(1, errorCode);
                break;
            }
        }
        // Reverse the digit string.
        char *p = digits.data();
        char *q = p + digits.length() - 1;
        while(p < q) {
            char digit = *p;
            *p++ = *q;
            *q-- = digit;
        }
    }
    if(U_FAILURE(errorCode)) { return; }
    int32_t pos = 0;
    do {
        // Skip leading zeros.
        while(pos < (digits.length() - 1) && digits[pos] == 0) { ++pos; }
        // Write a sequence of CEs for at most 254 digits at a time.
        int32_t segmentLength = digits.length() - pos;
        if(segmentLength > 254) { segmentLength = 254; }
        appendNumericSegmentCEs(digits.data() + pos, segmentLength, errorCode);
        pos += segmentLength;
    } while(U_SUCCESS(errorCode) && pos < digits.length());
}

void
CollationIterator::appendNumericSegmentCEs(const char *digits, int32_t length, UErrorCode &errorCode) {
    U_ASSERT(1 <= length && length <= 254);
    U_ASSERT(length == 1 || digits[0] != 0);
    uint32_t numericPrimary = data->numericPrimary;
    // Note: We use primary byte values 2..255: digits are not compressible.
    if(length <= 7) {
        // Very dense encoding for small numbers.
        int32_t value = digits[0];
        for(int32_t i = 1; i < length; ++i) {
            value = value * 10 + digits[i];
        }
        // Primary weight second byte values:
        //     74 byte values   2.. 75 for small numbers in two-byte primary weights.
        //     40 byte values  76..115 for medium numbers in three-byte primary weights.
        //     16 byte values 116..131 for large numbers in four-byte primary weights.
        //    124 byte values 132..255 for very large numbers with 4..127 digit pairs.
        int32_t firstByte = 2;
        int32_t numBytes = 74;
        if(value < numBytes) {
            // Two-byte primary for 0..73, good for day & month numbers etc.
            uint32_t primary = numericPrimary | ((firstByte + value) << 16);
            ceBuffer.append(Collation::makeCE(primary), errorCode);
            return;
        }
        value -= numBytes;
        firstByte += numBytes;
        numBytes = 40;
        if(value < numBytes * 254) {
            // Three-byte primary for 74..10233=74+40*254-1, good for year numbers and more.
            uint32_t primary = numericPrimary |
                ((firstByte + value / 254) << 16) | ((2 + value % 254) << 8);
            ceBuffer.append(Collation::makeCE(primary), errorCode);
            return;
        }
        value -= numBytes * 254;
        firstByte += numBytes;
        numBytes = 16;
        if(value < numBytes * 254 * 254) {
            // Four-byte primary for 10234..1042489=10234+16*254*254-1.
            uint32_t primary = numericPrimary | (2 + value % 254);
            value /= 254;
            primary |= (2 + value % 254) << 8;
            value /= 254;
            primary |= (firstByte + value % 254) << 16;
            ceBuffer.append(Collation::makeCE(primary), errorCode);
            return;
        }
        // original value > 1042489
    }
    U_ASSERT(length >= 7);

    // The second primary byte value 132..255 indicates the number of digit pairs (4..127),
    // then we generate primary bytes with those pairs.
    // Omit trailing 00 pairs.
    // Decrement the value for the last pair.

    // Set the exponent. 4 pairs->132, 5 pairs->133, ..., 127 pairs->255.
    int32_t numPairs = (length + 1) / 2;
    uint32_t primary = numericPrimary | ((132 - 4 + numPairs) << 16);
    // Find the length without trailing 00 pairs.
    while(digits[length - 1] == 0 && digits[length - 2] == 0) {
        length -= 2;
    }
    // Read the first pair.
    uint32_t pair;
    int32_t pos;
    if(length & 1) {
        // Only "half a pair" if we have an odd number of digits.
        pair = digits[0];
        pos = 1;
    } else {
        pair = digits[0] * 10 + digits[1];
        pos = 2;
    }
    pair = 11 + 2 * pair;
    // Add the pairs of digits between pos and length.
    int32_t shift = 8;
    while(pos < length) {
        if(shift == 0) {
            // Every three pairs/bytes we need to store a 4-byte-primary CE
            // and start with a new CE with the '0' primary lead byte.
            primary |= pair;
            ceBuffer.append(Collation::makeCE(primary), errorCode);
            primary = numericPrimary;
            shift = 16;
        } else {
            primary |= pair << shift;
            shift -= 8;
        }
        pair = 11 + 2 * (digits[pos] * 10 + digits[pos + 1]);
        pos += 2;
    }
    primary |= (pair - 1) << shift;
    ceBuffer.append(Collation::makeCE(primary), errorCode);
}

int64_t
CollationIterator::previousCE(UVector32 &offsets, UErrorCode &errorCode) {
    if(ceBuffer.length > 0) {
        // Return the previous buffered CE.
        return ceBuffer.get(--ceBuffer.length);
    }
    offsets.removeAllElements();
    int32_t limitOffset = getOffset();
    UChar32 c = previousCodePoint(errorCode);
    if(c < 0) { return Collation::NO_CE; }
    if(data->isUnsafeBackward(c, isNumeric)) {
        return previousCEUnsafe(c, offsets, errorCode);
    }
    // Simple, safe-backwards iteration:
    // Get a CE going backwards, handle prefixes but no contractions.
    uint32_t ce32 = data->getCE32(c);
    const CollationData *d;
    if(ce32 == Collation::FALLBACK_CE32) {
        d = data->base;
        ce32 = d->getCE32(c);
    } else {
        d = data;
    }
    if(Collation::isSimpleOrLongCE32(ce32)) {
        return Collation::ceFromCE32(ce32);
    }
    appendCEsFromCE32(d, c, ce32, false, errorCode);
    if(U_SUCCESS(errorCode)) {
        if(ceBuffer.length > 1) {
            offsets.addElement(getOffset(), errorCode);
            // For an expansion, the offset of each non-initial CE is the limit offset,
            // consistent with forward iteration.
            while(offsets.size() <= ceBuffer.length) {
                offsets.addElement(limitOffset, errorCode);
            }
        }
        return ceBuffer.get(--ceBuffer.length);
    } else {
        return Collation::NO_CE_PRIMARY;
    }
}

int64_t
CollationIterator::previousCEUnsafe(UChar32 c, UVector32 &offsets, UErrorCode &errorCode) {
    // We just move through the input counting safe and unsafe code points
    // without collecting the unsafe-backward substring into a buffer and
    // switching to it.
    // This is to keep the logic simple. Otherwise we would have to handle
    // prefix matching going before the backward buffer, switching
    // to iteration and back, etc.
    // In the most important case of iterating over a normal string,
    // reading from the string itself is already maximally fast.
    // The only drawback there is that after getting the CEs we always
    // skip backward to the safe character rather than switching out
    // of a backwardBuffer.
    // But this should not be the common case for previousCE(),
    // and correctness and maintainability are more important than
    // complex optimizations.
    // Find the first safe character before c.
    int32_t numBackward = 1;
    while((c = previousCodePoint(errorCode)) >= 0) {
        ++numBackward;
        if(!data->isUnsafeBackward(c, isNumeric)) {
            break;
        }
    }
    // Set the forward iteration limit.
    // Note: This counts code points.
    // We cannot enforce a limit in the middle of a surrogate pair or similar.
    numCpFwd = numBackward;
    // Reset the forward iterator.
    cesIndex = 0;
    U_ASSERT(ceBuffer.length == 0);
    // Go forward and collect the CEs.
    int32_t offset = getOffset();
    while(numCpFwd > 0) {
        // nextCE() normally reads one code point.
        // Contraction matching and digit specials read more and check numCpFwd.
        --numCpFwd;
        // Append one or more CEs to the ceBuffer.
        (void)nextCE(errorCode);
        U_ASSERT(U_FAILURE(errorCode) || ceBuffer.get(ceBuffer.length - 1) != Collation::NO_CE);
        // No need to loop for getting each expansion CE from nextCE().
        cesIndex = ceBuffer.length;
        // However, we need to write an offset for each CE.
        // This is for CollationElementIterator::getOffset() to return
        // intermediate offsets from the unsafe-backwards segment.
        U_ASSERT(offsets.size() < ceBuffer.length);
        offsets.addElement(offset, errorCode);
        // For an expansion, the offset of each non-initial CE is the limit offset,
        // consistent with forward iteration.
        offset = getOffset();
        while(offsets.size() < ceBuffer.length) {
            offsets.addElement(offset, errorCode);
        }
    }
    U_ASSERT(offsets.size() == ceBuffer.length);
    // End offset corresponding to just after the unsafe-backwards segment.
    offsets.addElement(offset, errorCode);
    // Reset the forward iteration limit
    // and move backward to before the segment for which we fetched CEs.
    numCpFwd = -1;
    backwardNumCodePoints(numBackward, errorCode);
    // Use the collected CEs and return the last one.
    cesIndex = 0;  // Avoid cesIndex > ceBuffer.length when that gets decremented.
    if(U_SUCCESS(errorCode)) {
        return ceBuffer.get(--ceBuffer.length);
    } else {
        return Collation::NO_CE_PRIMARY;
    }
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
