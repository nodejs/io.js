// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2002-2006, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
*/
#include "unicode/usetiter.h"
#include "unicode/uniset.h"
#include "unicode/unistr.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UnicodeSetIterator)

/**
 * Create an iterator
 * @param set set to iterate over
 */
UnicodeSetIterator::UnicodeSetIterator(const UnicodeSet& uSet) {
    cpString  = nullptr;
    reset(uSet);
}

/**
 * Create an iterator. Convenience for when the contents are to be set later.
 */
UnicodeSetIterator::UnicodeSetIterator() {
    this->set = nullptr;
    cpString  = nullptr;
    reset();
}

UnicodeSetIterator::~UnicodeSetIterator() {
    delete cpString;
}

/**
 * Returns the next element in the set.
 * @return true if there was another element in the set.
 * if so, if codepoint == IS_STRING, the value is a string in the string field
 * else the value is a single code point in the codepoint field.
 * <br>You are guaranteed that the codepoints are in sorted order, and the strings are in sorted order,
 * and that all code points are returned before any strings are returned.
 * <br>Note also that the codepointEnd is undefined after calling this method.
 */
UBool UnicodeSetIterator::next() {
    if (nextElement <= endElement) {
        codepoint = codepointEnd = nextElement++;
        string = nullptr;
        return true;
    }
    if (range < endRange) {
        loadRange(++range);
        codepoint = codepointEnd = nextElement++;
        string = nullptr;
        return true;
    }

    if (nextString >= stringCount) return false;
    codepoint = static_cast<UChar32>(IS_STRING); // signal that value is actually a string
    string = static_cast<const UnicodeString*>(set->strings_->elementAt(nextString++));
    return true;
}

/**
 * @return true if there was another element in the set.
 * if so, if codepoint == IS_STRING, the value is a string in the string field
 * else the value is a range of codepoints in the <codepoint, codepointEnd> fields.
 * <br>Note that the codepoints are in sorted order, and the strings are in sorted order,
 * and that all code points are returned before any strings are returned.
 * <br>You are guaranteed that the ranges are in sorted order, and the strings are in sorted order,
 * and that all ranges are returned before any strings are returned.
 * <br>You are also guaranteed that ranges are disjoint and non-contiguous.
 * <br>Note also that the codepointEnd is undefined after calling this method.
 */
UBool UnicodeSetIterator::nextRange() {
    string = nullptr;
    if (nextElement <= endElement) {
        codepointEnd = endElement;
        codepoint = nextElement;
        nextElement = endElement+1;
        return true;
    }
    if (range < endRange) {
        loadRange(++range);
        codepointEnd = endElement;
        codepoint = nextElement;
        nextElement = endElement+1;
        return true;
    }

    if (nextString >= stringCount) return false;
    codepoint = static_cast<UChar32>(IS_STRING); // signal that value is actually a string
    string = static_cast<const UnicodeString*>(set->strings_->elementAt(nextString++));
    return true;
}

/**
 *@param set the set to iterate over. This allows reuse of the iterator.
 */
void UnicodeSetIterator::reset(const UnicodeSet& uSet) {
    this->set = &uSet;
    reset();
}

/**
 * Resets to the start, to allow the iteration to start over again.
 */
void UnicodeSetIterator::reset() {
    if (set == nullptr) {
        // Set up indices to empty iteration
        endRange = -1;
        stringCount = 0;
    } else {
        endRange = set->getRangeCount() - 1;
        stringCount = set->stringsSize();
    }
    range = 0;
    endElement = -1;
    nextElement = 0;            
    if (endRange >= 0) {
        loadRange(range);
    }
    nextString = 0;
    string = nullptr;
}

void UnicodeSetIterator::loadRange(int32_t iRange) {
    nextElement = set->getRangeStart(iRange);
    endElement = set->getRangeEnd(iRange);
}


const UnicodeString& UnicodeSetIterator::getString()  {
    if (string == nullptr && codepoint != static_cast<UChar32>(IS_STRING)) {
       if (cpString == nullptr) {
          cpString = new UnicodeString();
       }
       if (cpString != nullptr) {
          cpString->setTo(codepoint);
       }
       string = cpString;
    }
    return *string;
}

U_NAMESPACE_END

//eof
