// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationiterator.h
*
* created on: 2010oct27
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONITERATOR_H__
#define __COLLATIONITERATOR_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"

U_NAMESPACE_BEGIN

class SkippedState;
class UCharsTrie;
class UVector32;

/* Large enough for CEs of most short strings. */
#define CEBUFFER_INITIAL_CAPACITY 40

// Export an explicit template instantiation of the MaybeStackArray that
//    is used as a data member of CEBuffer.
//
//    When building DLLs for Windows this is required even though
//    no direct access to the MaybeStackArray leaks out of the i18n library.
//
// See digitlst.h, pluralaffix.h, datefmt.h, and others for similar examples.
//
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API MaybeStackArray<int64_t, CEBUFFER_INITIAL_CAPACITY>;
#endif

/**
 * Collation element iterator and abstract character iterator.
 *
 * When a method returns a code point value, it must be in 0..10FFFF,
 * except it can be negative as a sentinel value.
 */
class U_I18N_API CollationIterator : public UObject {
private:
    class U_I18N_API CEBuffer {
    private:
        /** Large enough for CEs of most short strings. */
        static const int32_t INITIAL_CAPACITY = CEBUFFER_INITIAL_CAPACITY;
    public:
        CEBuffer() : length(0) {}
        ~CEBuffer();

        inline void append(int64_t ce, UErrorCode &errorCode) {
            if(length < INITIAL_CAPACITY || ensureAppendCapacity(1, errorCode)) {
                buffer[length++] = ce;
            }
        }

        inline void appendUnsafe(int64_t ce) {
            buffer[length++] = ce;
        }

        UBool ensureAppendCapacity(int32_t appCap, UErrorCode &errorCode);

        inline UBool incLength(UErrorCode &errorCode) {
            // Use INITIAL_CAPACITY for a very simple fastpath.
            // (Rather than buffer.getCapacity().)
            if(length < INITIAL_CAPACITY || ensureAppendCapacity(1, errorCode)) {
                ++length;
                return true;
            } else {
                return false;
            }
        }

        inline int64_t set(int32_t i, int64_t ce) {
            return buffer[i] = ce;
        }
        inline int64_t get(int32_t i) const { return buffer[i]; }

        const int64_t *getCEs() const { return buffer.getAlias(); }

        int32_t length;

    private:
        CEBuffer(const CEBuffer &) = delete;
        void operator=(const CEBuffer &) = delete;

        MaybeStackArray<int64_t, INITIAL_CAPACITY> buffer;
    };

public:
    CollationIterator(const CollationData *d, UBool numeric)
            : trie(d->trie),
              data(d),
              cesIndex(0),
              skipped(nullptr),
              numCpFwd(-1),
              isNumeric(numeric) {}

    virtual ~CollationIterator();

    virtual bool operator==(const CollationIterator &other) const;
    inline bool operator!=(const CollationIterator &other) const {
        return !operator==(other);
    }

    /**
     * Resets the iterator state and sets the position to the specified offset.
     * Subclasses must implement, and must call the parent class method,
     * or CollationIterator::reset().
     */
    virtual void resetToOffset(int32_t newOffset) = 0;

    virtual int32_t getOffset() const = 0;

    /**
     * Returns the next collation element.
     */
    inline int64_t nextCE(UErrorCode &errorCode) {
        if(cesIndex < ceBuffer.length) {
            // Return the next buffered CE.
            return ceBuffer.get(cesIndex++);
        }
        // assert cesIndex == ceBuffer.length;
        if(!ceBuffer.incLength(errorCode)) {
            return Collation::NO_CE;
        }
        UChar32 c;
        uint32_t ce32 = handleNextCE32(c, errorCode);
        uint32_t t = ce32 & 0xff;
        if(t < Collation::SPECIAL_CE32_LOW_BYTE) {  // Forced-inline of isSpecialCE32(ce32).
            // Normal CE from the main data.
            // Forced-inline of ceFromSimpleCE32(ce32).
            return ceBuffer.set(cesIndex++,
                    (static_cast<int64_t>(ce32 & 0xffff0000) << 32) | ((ce32 & 0xff00) << 16) | (t << 8));
        }
        const CollationData *d;
        // The compiler should be able to optimize the previous and the following
        // comparisons of t with the same constant.
        if(t == Collation::SPECIAL_CE32_LOW_BYTE) {
            if(c < 0) {
                return ceBuffer.set(cesIndex++, Collation::NO_CE);
            }
            d = data->base;
            ce32 = d->getCE32(c);
            t = ce32 & 0xff;
            if(t < Collation::SPECIAL_CE32_LOW_BYTE) {
                // Normal CE from the base data.
                return ceBuffer.set(cesIndex++,
                        (static_cast<int64_t>(ce32 & 0xffff0000) << 32) | ((ce32 & 0xff00) << 16) | (t << 8));
            }
        } else {
            d = data;
        }
        if(t == Collation::LONG_PRIMARY_CE32_LOW_BYTE) {
            // Forced-inline of ceFromLongPrimaryCE32(ce32).
            return ceBuffer.set(cesIndex++,
                    (static_cast<int64_t>(ce32 - t) << 32) | Collation::COMMON_SEC_AND_TER_CE);
        }
        return nextCEFromCE32(d, c, ce32, errorCode);
    }

    /**
     * Fetches all CEs.
     * @return getCEsLength()
     */
    int32_t fetchCEs(UErrorCode &errorCode);

    /**
     * Overwrites the current CE (the last one returned by nextCE()).
     */
    void setCurrentCE(int64_t ce) {
        // assert cesIndex > 0;
        ceBuffer.set(cesIndex - 1, ce);
    }

    /**
     * Returns the previous collation element.
     */
    int64_t previousCE(UVector32 &offsets, UErrorCode &errorCode);

    inline int32_t getCEsLength() const {
        return ceBuffer.length;
    }

    inline int64_t getCE(int32_t i) const {
        return ceBuffer.get(i);
    }

    const int64_t *getCEs() const {
        return ceBuffer.getCEs();
    }

    void clearCEs() {
        cesIndex = ceBuffer.length = 0;
    }

    void clearCEsIfNoneRemaining() {
        if(cesIndex == ceBuffer.length) { clearCEs(); }
    }

    /**
     * Returns the next code point (with post-increment).
     * Public for identical-level comparison and for testing.
     */
    virtual UChar32 nextCodePoint(UErrorCode &errorCode) = 0;

    /**
     * Returns the previous code point (with pre-decrement).
     * Public for identical-level comparison and for testing.
     */
    virtual UChar32 previousCodePoint(UErrorCode &errorCode) = 0;

protected:
    CollationIterator(const CollationIterator &other);

    void reset();

    /**
     * Returns the next code point and its local CE32 value.
     * Returns Collation::FALLBACK_CE32 at the end of the text (c<0)
     * or when c's CE32 value is to be looked up in the base data (fallback).
     *
     * The code point is used for fallbacks, context and implicit weights.
     * It is ignored when the returned CE32 is not special (e.g., FFFD_CE32).
     */
    virtual uint32_t handleNextCE32(UChar32 &c, UErrorCode &errorCode);

    /**
     * Called when handleNextCE32() returns a LEAD_SURROGATE_TAG for a lead surrogate code unit.
     * Returns the trail surrogate in that case and advances past it,
     * if a trail surrogate follows the lead surrogate.
     * Otherwise returns any other code unit and does not advance.
     */
    virtual char16_t handleGetTrailSurrogate();

    /**
     * Called when handleNextCE32() returns with c==0, to see whether it is a NUL terminator.
     * (Not needed in Java.)
     */
    virtual UBool foundNULTerminator();

    /**
     * @return false if surrogate code points U+D800..U+DFFF
     *         map to their own implicit primary weights (for UTF-16),
     *         or true if they map to CE(U+FFFD) (for UTF-8)
     */
    virtual UBool forbidSurrogateCodePoints() const;

    virtual void forwardNumCodePoints(int32_t num, UErrorCode &errorCode) = 0;

    virtual void backwardNumCodePoints(int32_t num, UErrorCode &errorCode) = 0;

    /**
     * Returns the CE32 from the data trie.
     * Normally the same as data->getCE32(), but overridden in the builder.
     * Call this only when the faster data->getCE32() cannot be used.
     */
    virtual uint32_t getDataCE32(UChar32 c) const;

    virtual uint32_t getCE32FromBuilderData(uint32_t ce32, UErrorCode &errorCode);

    void appendCEsFromCE32(const CollationData *d, UChar32 c, uint32_t ce32,
                           UBool forward, UErrorCode &errorCode);

    // Main lookup trie of the data object.
    const UTrie2 *trie;
    const CollationData *data;

private:
    int64_t nextCEFromCE32(const CollationData *d, UChar32 c, uint32_t ce32,
                           UErrorCode &errorCode);

    uint32_t getCE32FromPrefix(const CollationData *d, uint32_t ce32,
                               UErrorCode &errorCode);

    UChar32 nextSkippedCodePoint(UErrorCode &errorCode);

    void backwardNumSkipped(int32_t n, UErrorCode &errorCode);

    uint32_t nextCE32FromContraction(
            const CollationData *d, uint32_t contractionCE32,
            const char16_t *p, uint32_t ce32, UChar32 c,
            UErrorCode &errorCode);

    uint32_t nextCE32FromDiscontiguousContraction(
            const CollationData *d, UCharsTrie &suffixes, uint32_t ce32,
            int32_t lookAhead, UChar32 c,
            UErrorCode &errorCode);

    /**
     * Returns the previous CE when data->isUnsafeBackward(c, isNumeric).
     */
    int64_t previousCEUnsafe(UChar32 c, UVector32 &offsets, UErrorCode &errorCode);

    /**
     * Turns a string of digits (bytes 0..9)
     * into a sequence of CEs that will sort in numeric order.
     *
     * Starts from this ce32's digit value and consumes the following/preceding digits.
     * The digits string must not be empty and must not have leading zeros.
     */
    void appendNumericCEs(uint32_t ce32, UBool forward, UErrorCode &errorCode);

    /**
     * Turns 1..254 digits into a sequence of CEs.
     * Called by appendNumericCEs() for each segment of at most 254 digits.
     */
    void appendNumericSegmentCEs(const char *digits, int32_t length, UErrorCode &errorCode);

    CEBuffer ceBuffer;
    int32_t cesIndex;

    SkippedState *skipped;

    // Number of code points to read forward, or -1.
    // Used as a forward iteration limit in previousCEUnsafe().
    int32_t numCpFwd;
    // Numeric collation (CollationSettings::NUMERIC).
    UBool isNumeric;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONITERATOR_H__
