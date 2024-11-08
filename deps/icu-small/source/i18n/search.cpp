// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2008,2010 IBM and others. All rights reserved.
**********************************************************************
*   Date        Name        Description
*  03/22/2000   helena      Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION && !UCONFIG_NO_BREAK_ITERATION

#include "unicode/brkiter.h"
#include "unicode/schriter.h"
#include "unicode/search.h"
#include "usrchimp.h"
#include "cmemory.h"

// public constructors and destructors -----------------------------------
U_NAMESPACE_BEGIN

SearchIterator::SearchIterator(const SearchIterator &other)
    : UObject(other)
{   
    m_breakiterator_            = other.m_breakiterator_;
    m_text_                     = other.m_text_;
    m_search_ = static_cast<USearch*>(uprv_malloc(sizeof(USearch)));
    m_search_->breakIter        = other.m_search_->breakIter;
    m_search_->isCanonicalMatch = other.m_search_->isCanonicalMatch;
    m_search_->isOverlap        = other.m_search_->isOverlap;
    m_search_->elementComparisonType = other.m_search_->elementComparisonType;
    m_search_->matchedIndex     = other.m_search_->matchedIndex;
    m_search_->matchedLength    = other.m_search_->matchedLength;
    m_search_->text             = other.m_search_->text;
    m_search_->textLength       = other.m_search_->textLength;
}

SearchIterator::~SearchIterator()
{
    if (m_search_ != nullptr) {
        uprv_free(m_search_);
    }
}

// public get and set methods ----------------------------------------

void SearchIterator::setAttribute(USearchAttribute       attribute,
                                  USearchAttributeValue  value,
                                  UErrorCode            &status)
{
    if (U_SUCCESS(status)) {
        switch (attribute)
        {
        case USEARCH_OVERLAP :
            m_search_->isOverlap = (value == USEARCH_ON ? true : false);
            break;
        case USEARCH_CANONICAL_MATCH :
            m_search_->isCanonicalMatch = (value == USEARCH_ON ? true : false);
            break;
        case USEARCH_ELEMENT_COMPARISON :
            if (value == USEARCH_PATTERN_BASE_WEIGHT_IS_WILDCARD || value == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD) {
                m_search_->elementComparisonType = static_cast<int16_t>(value);
            } else {
                m_search_->elementComparisonType = 0;
            }
            break;
        default:
            status = U_ILLEGAL_ARGUMENT_ERROR;
        }
    }
    if (value == USEARCH_ATTRIBUTE_VALUE_COUNT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

USearchAttributeValue SearchIterator::getAttribute(
                                          USearchAttribute  attribute) const
{
    switch (attribute) {
    case USEARCH_OVERLAP :
        return (m_search_->isOverlap ? USEARCH_ON : USEARCH_OFF);
    case USEARCH_CANONICAL_MATCH :
        return (m_search_->isCanonicalMatch ? USEARCH_ON : USEARCH_OFF);
    case USEARCH_ELEMENT_COMPARISON :
        {
            int16_t value = m_search_->elementComparisonType;
            if (value == USEARCH_PATTERN_BASE_WEIGHT_IS_WILDCARD || value == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD) {
                return static_cast<USearchAttributeValue>(value);
            } else {
                return USEARCH_STANDARD_ELEMENT_COMPARISON;
            }
        }
    default :
        return USEARCH_DEFAULT;
    }
}
    
int32_t SearchIterator::getMatchedStart() const
{
    return m_search_->matchedIndex;
}

int32_t SearchIterator::getMatchedLength() const
{
    return m_search_->matchedLength;
}
    
void SearchIterator::getMatchedText(UnicodeString &result) const
{
    int32_t matchedindex  = m_search_->matchedIndex;
    int32_t     matchedlength = m_search_->matchedLength;
    if (matchedindex != USEARCH_DONE && matchedlength != 0) {
        result.setTo(m_search_->text + matchedindex, matchedlength); 
    }
    else {
        result.remove();
    }
}
    
void SearchIterator::setBreakIterator(BreakIterator *breakiter, 
                                      UErrorCode &status)
{
    if (U_SUCCESS(status)) {
#if 0
        m_search_->breakIter = nullptr;
        // the c++ breakiterator may not make use of ubreakiterator.
        // so we'll have to keep track of it ourselves.
#else
        // Well, gee... the Constructors that take a BreakIterator
        // all cast the BreakIterator to a UBreakIterator and
        // pass it to the corresponding usearch_openFromXXX
        // routine, so there's no reason not to do this.
        //
        // Besides, a UBreakIterator is a BreakIterator, so
        // any subclass of BreakIterator should work fine here...
        m_search_->breakIter = reinterpret_cast<UBreakIterator*>(breakiter);
#endif
        
        m_breakiterator_ = breakiter;
    }
}
    
const BreakIterator * SearchIterator::getBreakIterator() const
{
    return m_breakiterator_;
}

void SearchIterator::setText(const UnicodeString &text, UErrorCode &status)
{
    if (U_SUCCESS(status)) {
        if (text.length() == 0) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        else {
            m_text_        = text;
            m_search_->text = m_text_.getBuffer();
            m_search_->textLength = m_text_.length();
        }
    }
}

void SearchIterator::setText(CharacterIterator &text, UErrorCode &status)
{
    if (U_SUCCESS(status)) {
        text.getText(m_text_);
        setText(m_text_, status);
    }
}
    
const UnicodeString & SearchIterator::getText() const
{
    return m_text_;
}

// operator overloading ----------------------------------------------

bool SearchIterator::operator==(const SearchIterator &that) const
{
    if (this == &that) {
        return true;
    }
    return (m_breakiterator_            == that.m_breakiterator_ &&
            m_search_->isCanonicalMatch == that.m_search_->isCanonicalMatch &&
            m_search_->isOverlap        == that.m_search_->isOverlap &&
            m_search_->elementComparisonType == that.m_search_->elementComparisonType &&
            m_search_->matchedIndex     == that.m_search_->matchedIndex &&
            m_search_->matchedLength    == that.m_search_->matchedLength &&
            m_search_->textLength       == that.m_search_->textLength &&
            getOffset() == that.getOffset() &&
            (m_search_->textLength == 0 ||
            (uprv_memcmp(m_search_->text, that.m_search_->text, 
                              m_search_->textLength * sizeof(char16_t)) == 0)));
}

// public methods ----------------------------------------------------

int32_t SearchIterator::first(UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return USEARCH_DONE;
    }
    setOffset(0, status);
    return handleNext(0, status);
}

int32_t SearchIterator::following(int32_t position, 
                                      UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return USEARCH_DONE;
    }
    setOffset(position, status);
    return handleNext(position, status);
}
    
int32_t SearchIterator::last(UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return USEARCH_DONE;
    }
    setOffset(m_search_->textLength, status);
    return handlePrev(m_search_->textLength, status);
}

int32_t SearchIterator::preceding(int32_t position, 
                                      UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return USEARCH_DONE;
    }
    setOffset(position, status);
    return handlePrev(position, status);
}

int32_t SearchIterator::next(UErrorCode &status)
{
    if (U_SUCCESS(status)) {
        int32_t offset = getOffset();
        int32_t matchindex  = m_search_->matchedIndex;
        int32_t     matchlength = m_search_->matchedLength;
        m_search_->reset = false;
        if (m_search_->isForwardSearching) {
            int32_t textlength = m_search_->textLength;
            if (offset == textlength || matchindex == textlength || 
                (matchindex != USEARCH_DONE && 
                matchindex + matchlength >= textlength)) {
                // not enough characters to match
                setMatchNotFound();
                return USEARCH_DONE; 
            }
        }
        else {
            // switching direction. 
            // if matchedIndex == USEARCH_DONE, it means that either a 
            // setOffset has been called or that previous ran off the text
            // string. the iterator would have been set to offset 0 if a 
            // match is not found.
            m_search_->isForwardSearching = true;
            if (m_search_->matchedIndex != USEARCH_DONE) {
                // there's no need to set the collation element iterator
                // the next call to next will set the offset.
                return matchindex;
            }
        }

        if (matchlength > 0) {
            // if matchlength is 0 we are at the start of the iteration
            if (m_search_->isOverlap) {
                offset ++;
            }
            else {
                offset += matchlength;
            }
        }
        return handleNext(offset, status);
    }
    return USEARCH_DONE;
}

int32_t SearchIterator::previous(UErrorCode &status)
{
    if (U_SUCCESS(status)) {
        int32_t offset;
        if (m_search_->reset) {
            offset                       = m_search_->textLength;
            m_search_->isForwardSearching = false;
            m_search_->reset              = false;
            setOffset(offset, status);
        }
        else {
            offset = getOffset();
        }
        
        int32_t matchindex = m_search_->matchedIndex;
        if (m_search_->isForwardSearching) {
            // switching direction. 
            // if matchedIndex == USEARCH_DONE, it means that either a 
            // setOffset has been called or that next ran off the text
            // string. the iterator would have been set to offset textLength if 
            // a match is not found.
            m_search_->isForwardSearching = false;
            if (matchindex != USEARCH_DONE) {
                return matchindex;
            }
        }
        else {
            if (offset == 0 || matchindex == 0) {
                // not enough characters to match
                setMatchNotFound();
                return USEARCH_DONE; 
            }
        }

        if (matchindex != USEARCH_DONE) {
            if (m_search_->isOverlap) {
                matchindex += m_search_->matchedLength - 2;
            }

            return handlePrev(matchindex, status); 
        }

        return handlePrev(offset, status);
    }

    return USEARCH_DONE;
}

void SearchIterator::reset()
{
    UErrorCode status = U_ZERO_ERROR;
    setMatchNotFound();
    setOffset(0, status);
    m_search_->isOverlap          = false;
    m_search_->isCanonicalMatch   = false;
    m_search_->elementComparisonType = 0;
    m_search_->isForwardSearching = true;
    m_search_->reset              = true;
}

// protected constructors and destructors -----------------------------

SearchIterator::SearchIterator()
{
    m_search_ = static_cast<USearch*>(uprv_malloc(sizeof(USearch)));
    m_search_->breakIter          = nullptr;
    m_search_->isOverlap          = false;
    m_search_->isCanonicalMatch   = false;
    m_search_->elementComparisonType = 0;
    m_search_->isForwardSearching = true;
    m_search_->reset              = true;
    m_search_->matchedIndex       = USEARCH_DONE;
    m_search_->matchedLength      = 0;
    m_search_->text               = nullptr;
    m_search_->textLength         = 0;
    m_breakiterator_              = nullptr;
}

SearchIterator::SearchIterator(const UnicodeString &text, 
                                     BreakIterator *breakiter) :
                                     m_breakiterator_(breakiter),
                                     m_text_(text)
{
    m_search_ = static_cast<USearch*>(uprv_malloc(sizeof(USearch)));
    m_search_->breakIter          = nullptr;
    m_search_->isOverlap          = false;
    m_search_->isCanonicalMatch   = false;
    m_search_->elementComparisonType = 0;
    m_search_->isForwardSearching = true;
    m_search_->reset              = true;
    m_search_->matchedIndex       = USEARCH_DONE;
    m_search_->matchedLength      = 0;
    m_search_->text               = m_text_.getBuffer();
    m_search_->textLength         = text.length();
}

SearchIterator::SearchIterator(CharacterIterator &text, 
                               BreakIterator     *breakiter) :
                               m_breakiterator_(breakiter)
{
    m_search_ = static_cast<USearch*>(uprv_malloc(sizeof(USearch)));
    m_search_->breakIter          = nullptr;
    m_search_->isOverlap          = false;
    m_search_->isCanonicalMatch   = false;
    m_search_->elementComparisonType = 0;
    m_search_->isForwardSearching = true;
    m_search_->reset              = true;
    m_search_->matchedIndex       = USEARCH_DONE;
    m_search_->matchedLength      = 0;
    text.getText(m_text_);
    m_search_->text               = m_text_.getBuffer();
    m_search_->textLength         = m_text_.length();
    m_breakiterator_             = breakiter;
}

// protected methods ------------------------------------------------------

SearchIterator & SearchIterator::operator=(const SearchIterator &that)
{
    if (this != &that) {
        m_breakiterator_            = that.m_breakiterator_;
        m_text_                     = that.m_text_;
        m_search_->breakIter        = that.m_search_->breakIter;
        m_search_->isCanonicalMatch = that.m_search_->isCanonicalMatch;
        m_search_->isOverlap        = that.m_search_->isOverlap;
        m_search_->elementComparisonType = that.m_search_->elementComparisonType;
        m_search_->matchedIndex     = that.m_search_->matchedIndex;
        m_search_->matchedLength    = that.m_search_->matchedLength;
        m_search_->text             = that.m_search_->text;
        m_search_->textLength       = that.m_search_->textLength;
    }
    return *this;
}

void SearchIterator::setMatchLength(int32_t length)
{
    m_search_->matchedLength = length;
}

void SearchIterator::setMatchStart(int32_t position)
{
    m_search_->matchedIndex = position;
}

void SearchIterator::setMatchNotFound() 
{
    setMatchStart(USEARCH_DONE);
    setMatchLength(0);
    UErrorCode status = U_ZERO_ERROR;
    // by default no errors should be returned here since offsets are within 
    // range.
    if (m_search_->isForwardSearching) {
        setOffset(m_search_->textLength, status);
    }
    else {
        setOffset(0, status);
    }
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_COLLATION */
