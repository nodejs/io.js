// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1996-2014, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

/*
* File coleitr.cpp
*
* Created by: Helena Shih
*
* Modification History:
*
*  Date      Name        Description
*
*  6/23/97   helena      Adding comments to make code more readable.
* 08/03/98   erm         Synched with 1.2 version of CollationElementIterator.java
* 12/10/99   aliu        Ported Thai collation support from Java.
* 01/25/01   swquek      Modified to a C++ wrapper calling C APIs (ucoliter.h)
* 02/19/01   swquek      Removed CollationElementIterator() since it is 
*                        private constructor and no calls are made to it
* 2012-2014  markus      Rewritten in C++ again.
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/chariter.h"
#include "unicode/coleitr.h"
#include "unicode/tblcoll.h"
#include "unicode/ustring.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "collationiterator.h"
#include "collationsets.h"
#include "collationtailoring.h"
#include "uassert.h"
#include "uhash.h"
#include "utf16collationiterator.h"
#include "uvectr32.h"

/* Constants --------------------------------------------------------------- */

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(CollationElementIterator)

/* CollationElementIterator public constructor/destructor ------------------ */

CollationElementIterator::CollationElementIterator(
                                         const CollationElementIterator& other) 
        : UObject(other), iter_(nullptr), rbc_(nullptr), otherHalf_(0), dir_(0), offsets_(nullptr) {
    *this = other;
}

CollationElementIterator::~CollationElementIterator()
{
    delete iter_;
    delete offsets_;
}

/* CollationElementIterator public methods --------------------------------- */

namespace {

uint32_t getFirstHalf(uint32_t p, uint32_t lower32) {
    return (p & 0xffff0000) | ((lower32 >> 16) & 0xff00) | ((lower32 >> 8) & 0xff);
}
uint32_t getSecondHalf(uint32_t p, uint32_t lower32) {
    return (p << 16) | ((lower32 >> 8) & 0xff00) | (lower32 & 0x3f);
}
UBool ceNeedsTwoParts(int64_t ce) {
    return (ce & INT64_C(0xffff00ff003f)) != 0;
}

}  // namespace

int32_t CollationElementIterator::getOffset() const
{
    if (dir_ < 0 && offsets_ != nullptr && !offsets_->isEmpty()) {
        // CollationIterator::previousCE() decrements the CEs length
        // while it pops CEs from its internal buffer.
        int32_t i = iter_->getCEsLength();
        if (otherHalf_ != 0) {
            // Return the trailing CE offset while we are in the middle of a 64-bit CE.
            ++i;
        }
        U_ASSERT(i < offsets_->size());
        return offsets_->elementAti(i);
    }
    return iter_->getOffset();
}

/**
* Get the ordering priority of the next character in the string.
* @return the next character's ordering. Returns NULLORDER if an error has 
*         occurred or if the end of string has been reached
*/
int32_t CollationElementIterator::next(UErrorCode& status)
{
    if (U_FAILURE(status)) { return NULLORDER; }
    if (dir_ > 1) {
        // Continue forward iteration. Test this first.
        if (otherHalf_ != 0) {
            uint32_t oh = otherHalf_;
            otherHalf_ = 0;
            return oh;
        }
    } else if (dir_ == 1) {
        // next() after setOffset()
        dir_ = 2;
    } else if (dir_ == 0) {
        // The iter_ is already reset to the start of the text.
        dir_ = 2;
    } else /* dir_ < 0 */ {
        // illegal change of direction
        status = U_INVALID_STATE_ERROR;
        return NULLORDER;
    }
    // No need to keep all CEs in the buffer when we iterate.
    iter_->clearCEsIfNoneRemaining();
    int64_t ce = iter_->nextCE(status);
    if (ce == Collation::NO_CE) { return NULLORDER; }
    // Turn the 64-bit CE into two old-style 32-bit CEs, without quaternary bits.
    uint32_t p = static_cast<uint32_t>(ce >> 32);
    uint32_t lower32 = static_cast<uint32_t>(ce);
    uint32_t firstHalf = getFirstHalf(p, lower32);
    uint32_t secondHalf = getSecondHalf(p, lower32);
    if (secondHalf != 0) {
        otherHalf_ = secondHalf | 0xc0;  // continuation CE
    }
    return firstHalf;
}

bool CollationElementIterator::operator!=(
                                  const CollationElementIterator& other) const
{
    return !(*this == other);
}

bool CollationElementIterator::operator==(
                                    const CollationElementIterator& that) const
{
    if (this == &that) {
        return true;
    }

    return
        (rbc_ == that.rbc_ || *rbc_ == *that.rbc_) &&
        otherHalf_ == that.otherHalf_ &&
        normalizeDir() == that.normalizeDir() &&
        string_ == that.string_ &&
        *iter_ == *that.iter_;
}

/**
* Get the ordering priority of the previous collation element in the string.
* @param status the error code status.
* @return the previous element's ordering. Returns NULLORDER if an error has 
*         occurred or if the start of string has been reached.
*/
int32_t CollationElementIterator::previous(UErrorCode& status)
{
    if (U_FAILURE(status)) { return NULLORDER; }
    if (dir_ < 0) {
        // Continue backwards iteration. Test this first.
        if (otherHalf_ != 0) {
            uint32_t oh = otherHalf_;
            otherHalf_ = 0;
            return oh;
        }
    } else if (dir_ == 0) {
        iter_->resetToOffset(string_.length());
        dir_ = -1;
    } else if (dir_ == 1) {
        // previous() after setOffset()
        dir_ = -1;
    } else /* dir_ > 1 */ {
        // illegal change of direction
        status = U_INVALID_STATE_ERROR;
        return NULLORDER;
    }
    if (offsets_ == nullptr) {
        offsets_ = new UVector32(status);
        if (offsets_ == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return NULLORDER;
        }
    }
    // If we already have expansion CEs, then we also have offsets.
    // Otherwise remember the trailing offset in case we need to
    // write offsets for an artificial expansion.
    int32_t limitOffset = iter_->getCEsLength() == 0 ? iter_->getOffset() : 0;
    int64_t ce = iter_->previousCE(*offsets_, status);
    if (ce == Collation::NO_CE) { return NULLORDER; }
    // Turn the 64-bit CE into two old-style 32-bit CEs, without quaternary bits.
    uint32_t p = static_cast<uint32_t>(ce >> 32);
    uint32_t lower32 = static_cast<uint32_t>(ce);
    uint32_t firstHalf = getFirstHalf(p, lower32);
    uint32_t secondHalf = getSecondHalf(p, lower32);
    if (secondHalf != 0) {
        if (offsets_->isEmpty()) {
            // When we convert a single 64-bit CE into two 32-bit CEs,
            // we need to make this artificial expansion behave like a normal expansion.
            // See CollationIterator::previousCE().
            offsets_->addElement(iter_->getOffset(), status);
            offsets_->addElement(limitOffset, status);
        }
        otherHalf_ = firstHalf;
        return secondHalf | 0xc0;  // continuation CE
    }
    return firstHalf;
}

/**
* Resets the cursor to the beginning of the string.
*/
void CollationElementIterator::reset()
{
    iter_ ->resetToOffset(0);
    otherHalf_ = 0;
    dir_ = 0;
}

void CollationElementIterator::setOffset(int32_t newOffset, 
                                         UErrorCode& status)
{
    if (U_FAILURE(status)) { return; }
    if (0 < newOffset && newOffset < string_.length()) {
        int32_t offset = newOffset;
        do {
            char16_t c = string_.charAt(offset);
            if (!rbc_->isUnsafe(c) ||
                    (U16_IS_LEAD(c) && !rbc_->isUnsafe(string_.char32At(offset)))) {
                break;
            }
            // Back up to before this unsafe character.
            --offset;
        } while (offset > 0);
        if (offset < newOffset) {
            // We might have backed up more than necessary.
            // For example, contractions "ch" and "cu" make both 'h' and 'u' unsafe,
            // but for text "chu" setOffset(2) should remain at 2
            // although we initially back up to offset 0.
            // Find the last safe offset no greater than newOffset by iterating forward.
            int32_t lastSafeOffset = offset;
            do {
                iter_->resetToOffset(lastSafeOffset);
                do {
                    iter_->nextCE(status);
                    if (U_FAILURE(status)) { return; }
                } while ((offset = iter_->getOffset()) == lastSafeOffset);
                if (offset <= newOffset) {
                    lastSafeOffset = offset;
                }
            } while (offset < newOffset);
            newOffset = lastSafeOffset;
        }
    }
    iter_->resetToOffset(newOffset);
    otherHalf_ = 0;
    dir_ = 1;
}

/**
* Sets the source to the new source string.
*/
void CollationElementIterator::setText(const UnicodeString& source,
                                       UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return;
    }

    string_ = source;
    const char16_t *s = string_.getBuffer();
    CollationIterator *newIter;
    UBool numeric = rbc_->settings->isNumeric();
    if (rbc_->settings->dontCheckFCD()) {
        newIter = new UTF16CollationIterator(rbc_->data, numeric, s, s, s + string_.length());
    } else {
        newIter = new FCDUTF16CollationIterator(rbc_->data, numeric, s, s, s + string_.length());
    }
    if (newIter == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    delete iter_;
    iter_ = newIter;
    otherHalf_ = 0;
    dir_ = 0;
}

// Sets the source to the new character iterator.
void CollationElementIterator::setText(CharacterIterator& source, 
                                       UErrorCode& status)
{
    if (U_FAILURE(status)) 
        return;

    source.getText(string_);
    setText(string_, status);
}

int32_t CollationElementIterator::strengthOrder(int32_t order) const
{
    UColAttributeValue s = static_cast<UColAttributeValue>(rbc_->settings->getStrength());
    // Mask off the unwanted differences.
    if (s == UCOL_PRIMARY) {
        order &= 0xffff0000;
    }
    else if (s == UCOL_SECONDARY) {
        order &= 0xffffff00;
    }

    return order;
}

/* CollationElementIterator private constructors/destructors --------------- */

/** 
* This is the "real" constructor for this class; it constructs an iterator
* over the source text using the specified collator
*/
CollationElementIterator::CollationElementIterator(
                                               const UnicodeString &source,
                                               const RuleBasedCollator *coll,
                                               UErrorCode &status)
        : iter_(nullptr), rbc_(coll), otherHalf_(0), dir_(0), offsets_(nullptr) {
    setText(source, status);
}

/** 
* This is the "real" constructor for this class; it constructs an iterator over 
* the source text using the specified collator
*/
CollationElementIterator::CollationElementIterator(
                                           const CharacterIterator &source,
                                           const RuleBasedCollator *coll,
                                           UErrorCode &status)
        : iter_(nullptr), rbc_(coll), otherHalf_(0), dir_(0), offsets_(nullptr) {
    // We only call source.getText() which should be const anyway.
    setText(const_cast<CharacterIterator &>(source), status);
}

/* CollationElementIterator private methods -------------------------------- */

const CollationElementIterator& CollationElementIterator::operator=(
                                         const CollationElementIterator& other)
{
    if (this == &other) {
        return *this;
    }

    CollationIterator *newIter;
    const FCDUTF16CollationIterator *otherFCDIter =
            dynamic_cast<const FCDUTF16CollationIterator *>(other.iter_);
    if(otherFCDIter != nullptr) {
        newIter = new FCDUTF16CollationIterator(*otherFCDIter, string_.getBuffer());
    } else {
        const UTF16CollationIterator *otherIter =
                dynamic_cast<const UTF16CollationIterator *>(other.iter_);
        if(otherIter != nullptr) {
            newIter = new UTF16CollationIterator(*otherIter, string_.getBuffer());
        } else {
            newIter = nullptr;
        }
    }
    if(newIter != nullptr) {
        delete iter_;
        iter_ = newIter;
        rbc_ = other.rbc_;
        otherHalf_ = other.otherHalf_;
        dir_ = other.dir_;

        string_ = other.string_;
    }
    if(other.dir_ < 0 && other.offsets_ != nullptr && !other.offsets_->isEmpty()) {
        UErrorCode errorCode = U_ZERO_ERROR;
        if(offsets_ == nullptr) {
            offsets_ = new UVector32(other.offsets_->size(), errorCode);
        }
        if(offsets_ != nullptr) {
            offsets_->assign(*other.offsets_, errorCode);
        }
    }
    return *this;
}

namespace {

class MaxExpSink : public ContractionsAndExpansions::CESink {
public:
    MaxExpSink(UHashtable *h, UErrorCode &ec) : maxExpansions(h), errorCode(ec) {}
    virtual ~MaxExpSink();
    virtual void handleCE(int64_t /*ce*/) override {}
    virtual void handleExpansion(const int64_t ces[], int32_t length) override {
        if (length <= 1) {
            // We do not need to add single CEs into the map.
            return;
        }
        int32_t count = 0;  // number of CE "halves"
        for (int32_t i = 0; i < length; ++i) {
            count += ceNeedsTwoParts(ces[i]) ? 2 : 1;
        }
        // last "half" of the last CE
        int64_t ce = ces[length - 1];
        uint32_t p = static_cast<uint32_t>(ce >> 32);
        uint32_t lower32 = static_cast<uint32_t>(ce);
        uint32_t lastHalf = getSecondHalf(p, lower32);
        if (lastHalf == 0) {
            lastHalf = getFirstHalf(p, lower32);
            U_ASSERT(lastHalf != 0);
        } else {
            lastHalf |= 0xc0;  // old-style continuation CE
        }
        if (count > uhash_igeti(maxExpansions, static_cast<int32_t>(lastHalf))) {
            uhash_iputi(maxExpansions, static_cast<int32_t>(lastHalf), count, &errorCode);
        }
    }

private:
    UHashtable *maxExpansions;
    UErrorCode &errorCode;
};

MaxExpSink::~MaxExpSink() {}

}  // namespace

UHashtable *
CollationElementIterator::computeMaxExpansions(const CollationData *data, UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return nullptr; }
    UHashtable *maxExpansions = uhash_open(uhash_hashLong, uhash_compareLong,
                                           uhash_compareLong, &errorCode);
    if (U_FAILURE(errorCode)) { return nullptr; }
    MaxExpSink sink(maxExpansions, errorCode);
    ContractionsAndExpansions(nullptr, nullptr, &sink, true).forData(data, errorCode);
    if (U_FAILURE(errorCode)) {
        uhash_close(maxExpansions);
        return nullptr;
    }
    return maxExpansions;
}

int32_t
CollationElementIterator::getMaxExpansion(int32_t order) const {
    return getMaxExpansion(rbc_->tailoring->maxExpansions, order);
}

int32_t
CollationElementIterator::getMaxExpansion(const UHashtable *maxExpansions, int32_t order) {
    if (order == 0) { return 1; }
    int32_t max;
    if(maxExpansions != nullptr && (max = uhash_igeti(maxExpansions, order)) != 0) {
        return max;
    }
    if ((order & 0xc0) == 0xc0) {
        // old-style continuation CE
        return 2;
    } else {
        return 1;
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_COLLATION */
